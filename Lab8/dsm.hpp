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
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

/**
 * Distributed Shared Memory (DSM) Implementation — Replicated Model
 *
 * - There are NUM_VARIABLES integer DSM variables, each process maintains a local copy.
 * - All operations (write, CAS, read) are performed on local state, but must be delivered
 *   in the same total order for all subscribers using Lamport clocks and acknowledgements.
 * - Only subscribers can modify or receive notifications for a variable.
 */

constexpr int NUM_PROCESSES = 4;
constexpr int NUM_VARIABLES = 5;

enum class MessageType {
    WRITE  = 1,
    CAS    = 2,
    ACK    = 3
};

class DistributedSharedMemory {
public:
    using ChangeCallback = std::function<void(int, int, int, int)>;

private:
    int rank_;
    int world_size_;
    bool verbose_;

    std::vector<int> variables_;
    mutable std::mutex variable_mutex_;

    std::map<int, std::set<int>> subscriptions_;
    mutable std::mutex subscriptions_mutex_;

    ChangeCallback change_callback_;
    std::mutex callback_mutex_;

    std::atomic<int> lamport_clock_;
    std::atomic<int> next_message_id_;

    struct PendingMessage {
        int timestamp;
        int sender;
        int msg_id;
        MessageType type;
        int var_id;
        int new_value;
        int expected;
    };

    struct PendingCompare {
        bool operator()(const PendingMessage& a, const PendingMessage& b) const {
            if (a.timestamp != b.timestamp) return a.timestamp > b.timestamp;
            if (a.sender != b.sender) return a.sender > b.sender;
            return a.msg_id > b.msg_id;
        }
    };

    std::priority_queue<PendingMessage, std::vector<PendingMessage>, PendingCompare> pending_messages_;
    mutable std::mutex pending_mutex_;

    std::map<std::pair<int,int>, std::set<int>> ack_sets_;
    mutable std::mutex ack_mutex_;

    std::map<std::pair<int,int>, bool> cas_result_;
    std::map<std::pair<int,int>, bool> cas_done_;
    std::mutex cas_mutex_;
    std::condition_variable cas_cv_;

    std::thread message_thread_;
    std::atomic<bool> running_;

public:
    DistributedSharedMemory(int rank, int world_size, bool verbose = false)
            : rank_(rank),
              world_size_(world_size),
              verbose_(verbose),
              variables_(NUM_VARIABLES, 0),
              lamport_clock_(0),
              next_message_id_(0),
              running_(true)
    {
        if (world_size != NUM_PROCESSES) {
            throw std::runtime_error("World size must be " + std::to_string(NUM_PROCESSES));
        }

        if (verbose_) {
            log("DSM initialized with " + std::to_string(NUM_VARIABLES) + " variables per process.");
        }

        message_thread_ = std::thread(&DistributedSharedMemory::messageProcessingLoop, this);
    }

    ~DistributedSharedMemory() {
        running_ = false;
        if (message_thread_.joinable()) message_thread_.join();
    }

    DistributedSharedMemory(const DistributedSharedMemory&) = delete;
    DistributedSharedMemory& operator=(const DistributedSharedMemory&) = delete;

    void subscribe(int variable_id, const std::set<int>& subscriber_ranks) {
        if (variable_id < 0 || variable_id >= NUM_VARIABLES) {
            throw std::runtime_error("Variable ID out of range");
        }
        if (subscriber_ranks.find(rank_) == subscriber_ranks.end()) {
            throw std::runtime_error("Process must be a subscriber to subscribe.");
        }
        {
            std::lock_guard<std::mutex> lock(subscriptions_mutex_);
            subscriptions_[variable_id] = subscriber_ranks;
        }
        if (verbose_) log("Subscribed to variable " + std::to_string(variable_id));
        incrementClock();
    }

    void setChangeCallback(ChangeCallback callback) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        change_callback_ = callback;
    }

    int read(int variable_id) {
        if (variable_id < 0 || variable_id >= NUM_VARIABLES)
            throw std::runtime_error("Invalid variable id");
        incrementClock();
        std::lock_guard<std::mutex> lock(variable_mutex_);
        return variables_[variable_id];
    }

    void write(int variable_id, int new_value) {
        ensureSubscribed(variable_id);
        incrementClock();
        int timestamp = lamport_clock_.load();
        int msg_id = next_message_id_++;
        PendingMessage msg{timestamp, rank_, msg_id, MessageType::WRITE, variable_id, new_value, 0};
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            pending_messages_.push(msg);
        }
        auto key = std::make_pair(rank_, msg_id);
        {
            std::lock_guard<std::mutex> lock(ack_mutex_);
            ack_sets_[key].insert(rank_);
        }
        std::set<int> subscribers = getSubscribers(variable_id);
        std::vector<int> buffer = {(int)MessageType::WRITE, variable_id, new_value, 0, rank_, msg_id, timestamp};
        for (int dest : subscribers) {
            if (dest == rank_) continue;
            MPI_Send(buffer.data(), buffer.size(), MPI_INT, dest, 0, MPI_COMM_WORLD);
        }
        if (verbose_) log("WRITE var=" + std::to_string(variable_id) + " value=" + std::to_string(new_value) + " | T=" + std::to_string(timestamp));
    }

    bool compareAndSwap(int variable_id, int expected, int new_value) {
        ensureSubscribed(variable_id);
        incrementClock();
        int timestamp = lamport_clock_.load();
        int msg_id = next_message_id_++;
        PendingMessage msg{timestamp, rank_, msg_id, MessageType::CAS, variable_id, new_value, expected};
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            pending_messages_.push(msg);
        }
        auto key = std::make_pair(rank_, msg_id);
        {
            std::lock_guard<std::mutex> lock(ack_mutex_);
            ack_sets_[key].insert(rank_);
        }
        {
            std::lock_guard<std::mutex> lock(cas_mutex_);
            cas_result_[key] = false;
            cas_done_[key] = false;
        }
        std::set<int> subscribers = getSubscribers(variable_id);
        std::vector<int> buffer = {(int)MessageType::CAS, variable_id, new_value, expected, rank_, msg_id, timestamp};
        for (int dest : subscribers) {
            if (dest == rank_) continue;
            MPI_Send(buffer.data(), buffer.size(), MPI_INT, dest, 0, MPI_COMM_WORLD);
        }
        if (verbose_) log("CAS var=" + std::to_string(variable_id) + " expected=" + std::to_string(expected) + " new=" + std::to_string(new_value) + " | T=" + std::to_string(timestamp));
        std::unique_lock<std::mutex> lock(cas_mutex_);
        cas_cv_.wait(lock, [this, key] { return cas_done_[key]; });
        bool success = cas_result_[key];
        cas_done_.erase(key);
        cas_result_.erase(key);
        if (verbose_) log("CAS result: " + std::string(success ? "SUCCESS" : "FAILED"));
        return success;
    }

    int getLamportClock() const { return lamport_clock_.load(); }

private:
    void incrementClock() { lamport_clock_++; }

    std::set<int> getSubscribers(int variable_id) const {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        auto it = subscriptions_.find(variable_id);
        if (it == subscriptions_.end()) return {};
        return it->second;
    }
    void ensureSubscribed(int variable_id) const {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        auto it = subscriptions_.find(variable_id);
        if (it == subscriptions_.end() || it->second.find(rank_) == it->second.end()) {
            throw std::runtime_error("Process not subscribed to variable " + std::to_string(variable_id));
        }
    }

    void messageProcessingLoop() {
        while (running_) {
            processMessages();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    void processMessages() {
        int flag = 0;
        MPI_Status status;
        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status);
        while (flag && running_) {
            int count = 0;
            MPI_Get_count(&status, MPI_INT, &count);
            std::vector<int> buffer(count);
            MPI_Recv(buffer.data(), count, MPI_INT, status.MPI_SOURCE, status.MPI_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MessageType type = (MessageType)buffer[0];
            int var_id = buffer[1];
            int new_value = buffer[2];
            int expected = buffer[3];
            int original_sender = buffer[4];
            int msg_id = buffer[5];
            int msg_timestamp = buffer[6];
            lamport_clock_ = std::max(lamport_clock_.load(), msg_timestamp) + 1;
            if (type == MessageType::WRITE || type == MessageType::CAS) {
                handleOperationMessage(type, var_id, new_value, expected, original_sender, msg_id, msg_timestamp);
            } else if (type == MessageType::ACK) {
                handleAckMessage(var_id, original_sender, msg_id, status.MPI_SOURCE);
            }
            MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status);
        }
        deliverPendingMessages();
    }
    void handleOperationMessage(MessageType type, int var_id, int new_value, int expected, int original_sender, int msg_id, int msg_timestamp) {
        std::set<int> subscribers = getSubscribers(var_id);
        if (subscribers.empty() || subscribers.find(rank_) == subscribers.end()) return;
        PendingMessage msg{msg_timestamp, original_sender, msg_id, type, var_id, new_value, expected};
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            pending_messages_.push(msg);
        }
        auto key = std::make_pair(original_sender, msg_id);
        {
            std::lock_guard<std::mutex> lock(ack_mutex_);
            ack_sets_[key].insert(original_sender);
            ack_sets_[key].insert(rank_);
        }
        std::vector<int> ack_buffer = {(int)MessageType::ACK, var_id, 0, 0, original_sender, msg_id, lamport_clock_.load()};
        for (int dest : subscribers) {
            if (dest == rank_) continue;
            MPI_Send(ack_buffer.data(), ack_buffer.size(), MPI_INT, dest, 0, MPI_COMM_WORLD);
        }
        if (verbose_) log("RECEIVE " + std::string(type == MessageType::WRITE ? "WRITE" : "CAS") +
                          " from " + std::to_string(original_sender) +
                          " var=" + std::to_string(var_id) +
                          " T=" + std::to_string(msg_timestamp));
    }
    void handleAckMessage(int var_id, int original_sender, int msg_id, int ack_sender) {
        std::set<int> subscribers = getSubscribers(var_id);
        if (subscribers.empty() || subscribers.find(rank_) == subscribers.end()) return;
        auto key = std::make_pair(original_sender, msg_id);
        {
            std::lock_guard<std::mutex> lock(ack_mutex_);
            ack_sets_[key].insert(original_sender);
            ack_sets_[key].insert(ack_sender);
        }
    }
    bool haveAllAcks(const PendingMessage& msg) const {
        std::set<int> subscribers = getSubscribers(msg.var_id);
        if (subscribers.empty()) return false;
        auto key = std::make_pair(msg.sender, msg.msg_id);
        std::lock_guard<std::mutex> lock(ack_mutex_);
        auto ack_it = ack_sets_.find(key);
        if (ack_it == ack_sets_.end()) return false;
        const auto& ack_set = ack_it->second;
        for (int subscriber : subscribers) {
            if (ack_set.find(subscriber) == ack_set.end()) return false;
        }
        return true;
    }
    void deliverPendingMessages() {
        while (true) {
            PendingMessage msg;
            bool deliver = false;
            {
                std::lock_guard<std::mutex> lock(pending_mutex_);
                if (pending_messages_.empty()) break;
                const PendingMessage& top = pending_messages_.top();
                if (!haveAllAcks(top)) break;
                msg = top;
                pending_messages_.pop();
                deliver = true;
            }
            if (!deliver) break;
            deliverMessage(msg);
        }
    }
    void deliverMessage(const PendingMessage& msg) {
        int old_value;
        {
            std::lock_guard<std::mutex> lock(variable_mutex_);
            old_value = variables_[msg.var_id];
            bool success = true;
            if (msg.type == MessageType::WRITE) {
                variables_[msg.var_id] = msg.new_value;
            } else if (msg.type == MessageType::CAS) {
                if (variables_[msg.var_id] == msg.expected) {
                    variables_[msg.var_id] = msg.new_value;
                    success = true;
                } else {
                    success = false;
                }
                if (msg.sender == rank_) {
                    auto key = std::make_pair(msg.sender, msg.msg_id);
                    std::lock_guard<std::mutex> lock_cas(cas_mutex_);
                    cas_result_[key] = success;
                    cas_done_[key] = true;
                    cas_cv_.notify_all();
                }
            }
        }
        // Notify callback (lock is dropped above)
        invokeCallback(msg.var_id, old_value, variables_[msg.var_id], msg.timestamp);
    }
    void invokeCallback(int var_id, int old_value, int new_value, int ts) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (change_callback_) change_callback_(var_id, old_value, new_value, ts);
    }
    void log(const std::string& message) const {
        std::cout << "[Rank " << rank_ << "] " << message << std::endl << std::flush;
    }
};
