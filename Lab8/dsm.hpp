#pragma once

#include <mpi.h>
#include <functional>
#include <map>
#include <set>
#include <vector>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <string>
#include <queue>

/**
 * Enhanced DSM Implementation with Lamport Logical Clocks (Lecture 10)
 *
 * Sequential Consistency achieved via:
 * 1. Each variable has a sequencer (lowest rank subscriber)
 * 2. Sequencer assigns Lamport timestamps to all updates
 * 3. MPI FIFO guarantees ordered delivery from sequencer
 * 4. Priority queue applies updates in timestamp order
 * 5. Result: All processes see identical update sequences
 */

enum class MessageType {
    WRITE_REQUEST = 1,
    UPDATE = 2,
    CAS_REQUEST = 3,
    CAS_RESPONSE = 4
};

class DistributedSharedMemory {
public:
    using ChangeCallback = std::function<void(int variable_id, int old_value, int new_value, int lamport_time)>;

private:
    int rank_;
    int world_size_;
    bool verbose_;

    // Lamport logical clock - implements happens-before ordering (Lecture 10)
    int lamport_clock_;

    std::map<int, int> variables_;
    std::map<int, std::set<int>> subscriptions_;
    ChangeCallback change_callback_;

    // Priority queue for ordered update processing
    struct PendingUpdate {
        int lamport_time;
        int variable_id;
        int old_value;
        int new_value;

        bool operator>(const PendingUpdate& other) const {
            if (lamport_time != other.lamport_time) {
                return lamport_time > other.lamport_time;
            }
            return variable_id > other.variable_id;
        }
    };

    std::priority_queue<PendingUpdate, std::vector<PendingUpdate>, std::greater<PendingUpdate>> pending_updates_;

public:
    DistributedSharedMemory(int rank, int world_size, bool verbose = false)
            : rank_(rank), world_size_(world_size), verbose_(verbose), lamport_clock_(0) {
        if (verbose_) log("DSM initialized with Lamport clock at 0");
    }

    void subscribe(int variable_id, const std::set<int>& subscriber_ranks) {
        if (subscriber_ranks.find(rank_) == subscriber_ranks.end()) {
            throw std::runtime_error("Process must be in the subscriber list to subscribe.");
        }
        subscriptions_[variable_id] = subscriber_ranks;
        variables_[variable_id] = 0;
        incrementClock();
        if (verbose_) log("Subscribed to variable " + std::to_string(variable_id) +
                          " | Clock: " + std::to_string(lamport_clock_));
    }

    void setChangeCallback(ChangeCallback callback) {
        change_callback_ = callback;
    }

    int read(int variable_id) {
        if (variables_.find(variable_id) == variables_.end()) {
            throw std::runtime_error("Variable not subscribed or found.");
        }
        incrementClock();
        return variables_.at(variable_id);
    }

    void write(int variable_id, int new_value) {
        incrementClock(); // Local event

        int sequencer = getSequencer(variable_id);
        std::vector<int> buffer = {
                (int)MessageType::WRITE_REQUEST,
                variable_id,
                new_value,
                lamport_clock_ // Include Lamport timestamp
        };

        if (sequencer == rank_) {
            if (verbose_) log("Handling WRITE locally | T=" + std::to_string(lamport_clock_));
            handleWriteRequest(buffer, rank_);
        } else {
            if (verbose_) log("WRITE var " + std::to_string(variable_id) +
                              " -> sequencer " + std::to_string(sequencer) +
                              " | T=" + std::to_string(lamport_clock_));
            MPI_Send(buffer.data(), buffer.size(), MPI_INT, sequencer, 0, MPI_COMM_WORLD);
        }
    }

    bool compareAndSwap(int variable_id, int expected, int new_value) {
        incrementClock(); // Local event

        int sequencer = getSequencer(variable_id);
        std::vector<int> request_buf = {
                (int)MessageType::CAS_REQUEST,
                variable_id,
                expected,
                new_value,
                lamport_clock_
        };

        if (sequencer == rank_) {
            if (verbose_) log("Handling CAS locally | T=" + std::to_string(lamport_clock_));
            return handleCasRequest(request_buf, rank_);
        } else {
            if (verbose_) log("CAS var " + std::to_string(variable_id) +
                              " -> sequencer " + std::to_string(sequencer) +
                              " | T=" + std::to_string(lamport_clock_));
            MPI_Send(request_buf.data(), request_buf.size(), MPI_INT, sequencer, 0, MPI_COMM_WORLD);

            // Use tag=1 to distinguish CAS response from regular messages
            int success_response;
            MPI_Recv(&success_response, 1, MPI_INT, sequencer, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            incrementClock();
            if (verbose_) log("CAS response: " + std::string(success_response ? "SUCCESS" : "FAILED") +
                              " | T=" + std::to_string(lamport_clock_));
            return success_response;
        }
    }

    void processMessages() {
        int flag = 0;
        MPI_Status status;
        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status);

        while (flag) {
            int count;
            MPI_Get_count(&status, MPI_INT, &count);
            std::vector<int> buffer(count);
            MPI_Recv(buffer.data(), count, MPI_INT, status.MPI_SOURCE, status.MPI_TAG,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // Update Lamport clock on message receipt (Lecture 10)
            if (buffer.size() >= 4) {
                int message_timestamp = buffer[buffer.size() - 1];
                lamport_clock_ = std::max(lamport_clock_, message_timestamp) + 1;
            } else {
                incrementClock();
            }

            handleMessage(buffer, status.MPI_SOURCE);

            MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status);
        }

        // Process pending updates in timestamp order
        processPendingUpdates();
    }

    int getLamportClock() const {
        return lamport_clock_;
    }

private:
    void incrementClock() {
        lamport_clock_++;
    }

    int getSequencer(int variable_id) {
        const auto& subscribers = subscriptions_.at(variable_id);
        if (subscribers.empty()) throw std::runtime_error("No subscribers for variable");
        return *subscribers.begin();
    }

    void handleMessage(const std::vector<int>& buffer, int source_rank) {
        MessageType type = (MessageType)buffer[0];
        switch (type) {
            case MessageType::WRITE_REQUEST: handleWriteRequest(buffer, source_rank); break;
            case MessageType::UPDATE: handleUpdate(buffer); break;
            case MessageType::CAS_REQUEST: handleCasRequest(buffer, source_rank); break;
            case MessageType::CAS_RESPONSE: break;
        }
    }

    void handleWriteRequest(const std::vector<int>& buffer, int source_rank) {
        int var_id = buffer[1];
        int new_val = buffer[2];
        int request_time = buffer[3];

        incrementClock();

        if (verbose_) log("Sequencer: WRITE from " + std::to_string(source_rank) +
                          " | ReqT=" + std::to_string(request_time) +
                          " | CurT=" + std::to_string(lamport_clock_));

        int old_val = variables_[var_id];
        std::vector<int> update_buf = {
                (int)MessageType::UPDATE,
                var_id,
                old_val,
                new_val,
                lamport_clock_ // Sequencer's timestamp
        };

        const auto& subscribers = subscriptions_.at(var_id);
        for (int dest_rank : subscribers) {
            if (dest_rank == rank_) {
                handleUpdate(update_buf);
            } else {
                MPI_Send(update_buf.data(), update_buf.size(), MPI_INT, dest_rank, 0, MPI_COMM_WORLD);
            }
        }
    }

    bool handleCasRequest(const std::vector<int>& buffer, int source_rank) {
        int var_id = buffer[1];
        int expected = buffer[2];
        int new_val = buffer[3];
        int request_time = buffer[4];

        incrementClock();

        if (verbose_) log("Sequencer: CAS from " + std::to_string(source_rank) +
                          " | Expected=" + std::to_string(expected) +
                          " | Current=" + std::to_string(variables_[var_id]) +
                          " | ReqT=" + std::to_string(request_time) +
                          " | CurT=" + std::to_string(lamport_clock_));

        bool success = false;
        if (variables_[var_id] == expected) {
            success = true;
            int old_val = variables_[var_id];

            // UPDATE LOCAL STATE IMMEDIATELY (sequencer applies its own update)
            variables_[var_id] = new_val;

            std::vector<int> update_buf = {
                    (int)MessageType::UPDATE,
                    var_id,
                    old_val,
                    new_val,
                    lamport_clock_
            };

            const auto& subscribers = subscriptions_.at(var_id);
            for (int dest_rank : subscribers) {
                if (dest_rank != rank_) {  // Don't send to self since we already updated
                    MPI_Send(update_buf.data(), update_buf.size(), MPI_INT, dest_rank, 0, MPI_COMM_WORLD);
                }
            }

            // Trigger callback for sequencer
            if (change_callback_) {
                change_callback_(var_id, old_val, new_val, lamport_clock_);
            }
        }

        int response = success ? 1 : 0;
        if (source_rank != rank_) {
            MPI_Send(&response, 1, MPI_INT, source_rank, 1, MPI_COMM_WORLD);
        }
        return success;
    }



    void handleUpdate(const std::vector<int>& buffer) {
        int var_id = buffer[1];
        int old_val = buffer[2];
        int new_val = buffer[3];
        int update_time = buffer[4];

        // Queue for ordered processing
        pending_updates_.push({update_time, var_id, old_val, new_val});
    }

    void processPendingUpdates() {
        while (!pending_updates_.empty()) {
            auto update = pending_updates_.top();
            pending_updates_.pop();

            int local_old_val = variables_[update.variable_id];
            variables_[update.variable_id] = update.new_value;

            if (verbose_) log("Applied UPDATE var " + std::to_string(update.variable_id) +
                              " | T=" + std::to_string(update.lamport_time) +
                              " | " + std::to_string(local_old_val) + " -> " +
                              std::to_string(update.new_value));

            if (change_callback_) {
                change_callback_(update.variable_id, local_old_val, update.new_value, update.lamport_time);
            }
        }
    }

    void log(const std::string& message) {
        std::cout << "[Rank " << rank_ << "] " << message << std::endl;
    }
};
