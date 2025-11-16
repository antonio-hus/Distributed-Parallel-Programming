#pragma once

#include <mpi.h>
#include <functional>
#include <map>
#include <set>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <queue>

/**
 * Distributed Shared Memory (DSM) with Lamport Totally-Ordered Multicast (no sequencer).
 *
 * - Each process keeps a Lamport logical clock.
 * - A write or CAS is multicast to all subscribers of that variable with a (timestamp, sender, msg_id).
 * - All processes insert received messages into a priority queue ordered by (timestamp, sender, msg_id).
 * - Each process sends ACKs; a message is DELIVERED only when all subscribers have ACKed it
 *   and it is at the head of the queue.
 * - This yields the same global total order of updates (writes + CAS) on all subscribers,
 *   satisfying the lab requirement that all processes see the same callback sequence.
 */

enum class MessageType {
    UPDATE = 1,  // simple write(var, new_value)
    CAS    = 2,  // compareAndSwap(var, expected, new_value)
    ACK    = 3   // acknowledgement for total-order multicast
};

class DistributedSharedMemory {
public:
    using ChangeCallback =
            std::function<void(int variable_id, int old_value, int new_value, int lamport_time)>;

private:
    int rank_;
    int world_size_;
    bool verbose_;

    // Lamport logical clock
    int lamport_clock_;

    // Per-process local copy of DSM variables
    std::map<int, int> variables_;

    // Static subscription sets: variable_id -> set of ranks
    std::map<int, std::set<int>> subscriptions_;

    ChangeCallback change_callback_;

    // Per-process monotonically increasing ID for DSM messages
    int next_message_id_;

    // Pending DSM operations waiting for total-order delivery
    struct PendingMessage {
        int timestamp;      // Lamport timestamp assigned at send
        int sender;         // original sender rank
        int msg_id;         // per-sender unique id
        MessageType type;   // UPDATE or CAS
        int var_id;
        int new_value;      // for UPDATE and CAS
        int expected;       // for CAS; ignored for UPDATE
    };

    struct PendingCompare {
        bool operator()(const PendingMessage& a, const PendingMessage& b) const {
            if (a.timestamp != b.timestamp) {
                return a.timestamp > b.timestamp; // min-heap by timestamp
            }
            if (a.sender != b.sender) {
                return a.sender > b.sender;       // tie-break by rank
            }
            return a.msg_id > b.msg_id;           // tie-break by local message id
        }
    };

    std::priority_queue<PendingMessage,
            std::vector<PendingMessage>,
            PendingCompare> pending_messages_;

    // For each (sender,msg_id) we track the ranks that have seen the message
    // (origin is always implicitly considered to have seen it)
    std::map<std::pair<int,int>, std::set<int>> ack_sets_;

    // CAS result tracking for the originating process
    std::map<std::pair<int,int>, bool> cas_result_;
    std::map<std::pair<int,int>, bool> cas_done_;

public:
    DistributedSharedMemory(int rank, int world_size, bool verbose = false)
            : rank_(rank),
              world_size_(world_size),
              verbose_(verbose),
              lamport_clock_(0),
              next_message_id_(0) {
        if (verbose_) log("DSM initialized (no sequencer, Lamport total order)");
    }

    void subscribe(int variable_id, const std::set<int>& subscriber_ranks) {
        if (subscriber_ranks.find(rank_) == subscriber_ranks.end()) {
            throw std::runtime_error("Process must be in the subscriber list to subscribe.");
        }
        subscriptions_[variable_id] = subscriber_ranks;
        variables_[variable_id] = 0;
        incrementClock();
        if (verbose_) {
            log("Subscribed to variable " + std::to_string(variable_id) +
                " | Clock=" + std::to_string(lamport_clock_));
        }
    }

    void setChangeCallback(ChangeCallback callback) {
        change_callback_ = callback;
    }

    int read(int variable_id) {
        auto it = variables_.find(variable_id);
        if (it == variables_.end()) {
            throw std::runtime_error("Variable not subscribed or found.");
        }
        incrementClock();
        return it->second;
    }

    // Simple write: totally ordered multicast to all subscribers of the variable
    void write(int variable_id, int new_value) {
        ensureSubscribed(variable_id);

        incrementClock();
        int timestamp = lamport_clock_;
        int msg_id = next_message_id_++;

        PendingMessage msg{
                timestamp,
                rank_,
                msg_id,
                MessageType::UPDATE,
                variable_id,
                new_value,
                0 // expected not used for UPDATE
        };

        // Insert locally into pending queue
        pending_messages_.push(msg);

        // Origin has trivially seen its own message
        auto key = std::make_pair(rank_, msg_id);
        ack_sets_[key].insert(rank_);

        // Multicast to all other subscribers of this variable
        const auto& subscribers = subscriptions_.at(variable_id);
        std::vector<int> buffer = {
                (int)MessageType::UPDATE,
                variable_id,
                new_value,
                0,             // expected unused
                rank_,         // original sender
                msg_id,
                timestamp
        };

        for (int dest : subscribers) {
            if (dest == rank_) continue; // local already added
            MPI_Send(buffer.data(), (int)buffer.size(), MPI_INT, dest, 0, MPI_COMM_WORLD);
        }

        if (verbose_) {
            log("WRITE var " + std::to_string(variable_id) +
                " = " + std::to_string(new_value) +
                " | T=" + std::to_string(timestamp));
        }
    }

    // CAS: totally ordered, returns success/failure after the CAS is globally ordered and applied
    bool compareAndSwap(int variable_id, int expected, int new_value) {
        ensureSubscribed(variable_id);

        incrementClock();
        int timestamp = lamport_clock_;
        int msg_id = next_message_id_++;

        PendingMessage msg{
                timestamp,
                rank_,
                msg_id,
                MessageType::CAS,
                variable_id,
                new_value,
                expected
        };

        // Insert locally into pending queue
        pending_messages_.push(msg);

        auto key = std::make_pair(rank_, msg_id);
        ack_sets_[key].insert(rank_);  // origin has seen its own message

        // Mark CAS as not yet decided
        cas_done_[key] = false;
        cas_result_[key] = false;

        // Multicast CAS to all other subscribers
        const auto& subscribers = subscriptions_.at(variable_id);
        std::vector<int> buffer = {
                (int)MessageType::CAS,
                variable_id,
                new_value,
                expected,
                rank_,     // original sender
                msg_id,
                timestamp
        };

        for (int dest : subscribers) {
            if (dest == rank_) continue;
            MPI_Send(buffer.data(), (int)buffer.size(), MPI_INT, dest, 0, MPI_COMM_WORLD);
        }

        if (verbose_) {
            log("CAS request var " + std::to_string(variable_id) +
                " expected=" + std::to_string(expected) +
                " new=" + std::to_string(new_value) +
                " | T=" + std::to_string(timestamp));
        }

        // Wait until this CAS is delivered in total order and decided
        while (!cas_done_[key]) {
            processMessages();
        }

        bool success = cas_result_[key];
        cas_done_.erase(key);
        cas_result_.erase(key);

        if (verbose_) {
            log(std::string("CAS result var ") + std::to_string(variable_id) +
                " -> " + (success ? "SUCCESS" : "FAILED"));
        }

        return success;
    }

    // Must be called periodically by the main program to receive and deliver messages
    void processMessages() {
        int flag = 0;
        MPI_Status status;

        // Receive all currently available messages
        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status);
        while (flag) {
            int count = 0;
            MPI_Get_count(&status, MPI_INT, &count);
            std::vector<int> buffer(count);

            MPI_Recv(buffer.data(), count, MPI_INT,
                     status.MPI_SOURCE, status.MPI_TAG,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            MessageType type = (MessageType)buffer[0];
            int var_id       = buffer[1];
            int new_value    = buffer[2];
            int expected     = buffer[3];
            int original_sender = buffer[4];
            int msg_id       = buffer[5];
            int msg_timestamp = buffer[6];

            // Update Lamport clock on receive
            lamport_clock_ = std::max(lamport_clock_, msg_timestamp) + 1;

            if (type == MessageType::UPDATE || type == MessageType::CAS) {
                handleDsmMessage(type,
                                 var_id,
                                 new_value,
                                 expected,
                                 original_sender,
                                 msg_id,
                                 msg_timestamp);
            } else if (type == MessageType::ACK) {
                handleAckMessage(var_id,
                                 original_sender,
                                 msg_id,
                                 status.MPI_SOURCE);
            }

            MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG,
                       MPI_COMM_WORLD, &flag, &status);
        }

        // Try to deliver messages in total order
        deliverPendingMessages();
    }

    int getLamportClock() const {
        return lamport_clock_;
    }

private:
    void incrementClock() {
        lamport_clock_++;
    }

    void ensureSubscribed(int variable_id) const {
        auto it = subscriptions_.find(variable_id);
        if (it == subscriptions_.end() ||
            it->second.find(rank_) == it->second.end()) {
            throw std::runtime_error("Process not subscribed to this variable.");
        }
    }

    void handleDsmMessage(MessageType type,
                          int var_id,
                          int new_value,
                          int expected,
                          int original_sender,
                          int msg_id,
                          int msg_timestamp) {
        // Ignore messages for variables this process does not know about
        auto subs_it = subscriptions_.find(var_id);
        if (subs_it == subscriptions_.end()) {
            return;
        }

        PendingMessage msg{
                msg_timestamp,
                original_sender,
                msg_id,
                type,
                var_id,
                new_value,
                expected
        };

        pending_messages_.push(msg);

        auto key = std::make_pair(original_sender, msg_id);
        auto& ack_set = ack_sets_[key];

        // Origin has trivially seen the message
        ack_set.insert(original_sender);
        // This receiver has now seen it
        ack_set.insert(rank_);

        // Send ACK to all subscribers of this variable
        const auto& subscribers = subs_it->second;
        std::vector<int> ack_buffer = {
                (int)MessageType::ACK,
                var_id,
                0,          // unused
                0,          // unused
                original_sender,
                msg_id,
                lamport_clock_
        };

        for (int dest : subscribers) {
            if (dest == rank_) continue;
            MPI_Send(ack_buffer.data(), (int)ack_buffer.size(), MPI_INT, dest, 0, MPI_COMM_WORLD);
        }

        if (verbose_) {
            log("Received DSM msg type=" + std::to_string((int)type) +
                " var=" + std::to_string(var_id) +
                " from " + std::to_string(original_sender) +
                " msg_id=" + std::to_string(msg_id) +
                " | T=" + std::to_string(msg_timestamp));
        }
    }

    void handleAckMessage(int var_id,
                          int original_sender,
                          int msg_id,
                          int ack_sender) {
        auto subs_it = subscriptions_.find(var_id);
        if (subs_it == subscriptions_.end()) {
            return;
        }

        auto key = std::make_pair(original_sender, msg_id);
        auto& ack_set = ack_sets_[key];

        // Origin always considered to have seen the message
        ack_set.insert(original_sender);
        // The sender of this ACK has seen it
        ack_set.insert(ack_sender);

        if (verbose_) {
            log("ACK for msg (" + std::to_string(original_sender) +
                "," + std::to_string(msg_id) +
                ") from " + std::to_string(ack_sender));
        }
    }

    bool haveAllAcks(const PendingMessage& msg) const {
        auto subs_it = subscriptions_.find(msg.var_id);
        if (subs_it == subscriptions_.end()) return false;

        auto key = std::make_pair(msg.sender, msg.msg_id);
        auto ack_it = ack_sets_.find(key);
        if (ack_it == ack_sets_.end()) return false;

        const auto& subscribers = subs_it->second;
        const auto& ack_set = ack_it->second;

        for (int r : subscribers) {
            if (ack_set.find(r) == ack_set.end()) {
                return false;
            }
        }
        return true;
    }

    void deliverPendingMessages() {
        bool delivered = true;

        // Keep delivering while the top message is deliverable
        while (delivered && !pending_messages_.empty()) {
            delivered = false;
            const PendingMessage& top = pending_messages_.top();

            if (!haveAllAcks(top)) {
                // Cannot yet deliver; some subscribers did not ACK
                break;
            }

            PendingMessage msg = top;
            pending_messages_.pop();
            delivered = true;

            int old_val = variables_[msg.var_id];
            bool cas_success = false;

            if (msg.type == MessageType::UPDATE) {
                variables_[msg.var_id] = msg.new_value;
                if (verbose_) {
                    log("DELIVER UPDATE var " + std::to_string(msg.var_id) +
                        ": " + std::to_string(old_val) +
                        " -> " + std::to_string(msg.new_value) +
                        " | T=" + std::to_string(msg.timestamp));
                }
                if (change_callback_) {
                    change_callback_(msg.var_id, old_val, msg.new_value, msg.timestamp);
                }
            } else if (msg.type == MessageType::CAS) {
                if (variables_[msg.var_id] == msg.expected) {
                    variables_[msg.var_id] = msg.new_value;
                    cas_success = true;
                    if (verbose_) {
                        log("DELIVER CAS SUCCESS var " + std::to_string(msg.var_id) +
                            ": " + std::to_string(old_val) +
                            " -> " + std::to_string(msg.new_value) +
                            " | T=" + std::to_string(msg.timestamp));
                    }
                    if (change_callback_) {
                        change_callback_(msg.var_id, old_val, msg.new_value, msg.timestamp);
                    }
                } else {
                    if (verbose_) {
                        log("DELIVER CAS FAIL var " + std::to_string(msg.var_id) +
                            " expected=" + std::to_string(msg.expected) +
                            " current=" + std::to_string(old_val) +
                            " | T=" + std::to_string(msg.timestamp));
                    }
                }

                // If this process originated the CAS, record the result
                if (msg.sender == rank_) {
                    auto key = std::make_pair(msg.sender, msg.msg_id);
                    cas_result_[key] = cas_success;
                    cas_done_[key] = true;
                }
            }
        }
    }

    void log(const std::string& message) const {
        std::cout << "[Rank " << rank_ << "] " << message << std::endl;
    }
};
