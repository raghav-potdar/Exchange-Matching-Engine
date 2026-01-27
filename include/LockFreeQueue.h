/**
 * @file LockFreeQueue.h
 * @brief Lock-free queue implementations for inter-thread communication.
 * 
 * Provides SPSC (Single-Producer Single-Consumer) and MPSC (Multiple-Producer
 * Single-Consumer) lock-free queues using atomic operations.
 */

#pragma once

#ifndef LOCKFREEQUEUE_H
#define LOCKFREEQUEUE_H

#include <atomic>
#include <array>
#include <cstddef>
#include <optional>
#include <new>

/**
 * @class SPSCQueue
 * @brief Single-Producer Single-Consumer lock-free queue.
 * 
 * A bounded, lock-free queue optimized for single producer and single
 * consumer threads. Uses atomic operations with appropriate memory
 * ordering for thread safety without locks.
 * 
 * @tparam T Element type (must be movable).
 * @tparam Capacity Queue capacity (must be power of 2).
 * 
 * @par Memory Layout
 * Head and tail pointers are cache-line aligned (64 bytes) to prevent
 * false sharing between producer and consumer threads.
 * 
 * @par Example Usage
 * @code
 * SPSCQueue<int, 1024> queue;
 * 
 * // Producer thread
 * queue.push(42);
 * 
 * // Consumer thread
 * if (auto item = queue.pop()) {
 *     process(*item);
 * }
 * @endcode
 */
template<typename T, size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    
public:
    SPSCQueue() : head_(0), tail_(0) {}
    
    /**
     * @brief Push an item to the queue (copy).
     * @param item Item to push.
     * @return true if successful, false if queue is full.
     */
    bool push(const T& item) {
        const size_t currentTail = tail_.load(std::memory_order_relaxed);
        const size_t nextTail = (currentTail + 1) & (Capacity - 1);
        
        if (nextTail == head_.load(std::memory_order_acquire)) {
            return false;
        }
        
        buffer_[currentTail] = item;
        tail_.store(nextTail, std::memory_order_release);
        return true;
    }
    
    /**
     * @brief Push an item to the queue (move).
     * @param item Item to push (moved).
     * @return true if successful, false if queue is full.
     */
    bool push(T&& item) {
        const size_t currentTail = tail_.load(std::memory_order_relaxed);
        const size_t nextTail = (currentTail + 1) & (Capacity - 1);
        
        if (nextTail == head_.load(std::memory_order_acquire)) {
            return false;
        }
        
        buffer_[currentTail] = std::move(item);
        tail_.store(nextTail, std::memory_order_release);
        return true;
    }
    
    /**
     * @brief Pop an item from the queue.
     * @return Item if available, std::nullopt if queue is empty.
     */
    std::optional<T> pop() {
        const size_t currentHead = head_.load(std::memory_order_relaxed);
        
        if (currentHead == tail_.load(std::memory_order_acquire)) {
            return std::nullopt;
        }
        
        T item = std::move(buffer_[currentHead]);
        head_.store((currentHead + 1) & (Capacity - 1), std::memory_order_release);
        return item;
    }
    
    /**
     * @brief Check if queue is empty (approximate).
     * @return true if empty at time of check.
     */
    bool empty() const {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief Get approximate queue size.
     * @return Number of items (may race with push/pop).
     */
    size_t size() const {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        return (tail - head + Capacity) & (Capacity - 1);
    }
    
private:
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
    alignas(64) std::array<T, Capacity> buffer_;
};

/**
 * @class MPSCQueue
 * @brief Multiple-Producer Single-Consumer queue.
 * 
 * A bounded queue that supports multiple producer threads but only
 * one consumer thread. Uses a spin-lock for producer synchronization.
 * 
 * @tparam T Element type (must be movable).
 * @tparam Capacity Queue capacity (must be power of 2).
 * 
 * @warning Only ONE thread may call pop(). Multiple threads may call push().
 */
template<typename T, size_t Capacity>
class MPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    
public:
    MPSCQueue() : head_(0), tail_(0), producerLock_(false) {}
    
    /**
     * @brief Push an item (thread-safe for multiple producers).
     * @param item Item to push.
     * @return true if successful, false if queue is full.
     */
    bool push(const T& item) {
        while (producerLock_.exchange(true, std::memory_order_acquire)) {
            // Spin wait
        }
        
        const size_t currentTail = tail_.load(std::memory_order_relaxed);
        const size_t nextTail = (currentTail + 1) & (Capacity - 1);
        
        if (nextTail == head_.load(std::memory_order_acquire)) {
            producerLock_.store(false, std::memory_order_release);
            return false;
        }
        
        buffer_[currentTail] = item;
        tail_.store(nextTail, std::memory_order_release);
        producerLock_.store(false, std::memory_order_release);
        return true;
    }
    
    /**
     * @brief Push an item with move semantics.
     * @param item Item to push (moved).
     * @return true if successful, false if queue is full.
     */
    bool push(T&& item) {
        while (producerLock_.exchange(true, std::memory_order_acquire)) {
            // Spin wait
        }
        
        const size_t currentTail = tail_.load(std::memory_order_relaxed);
        const size_t nextTail = (currentTail + 1) & (Capacity - 1);
        
        if (nextTail == head_.load(std::memory_order_acquire)) {
            producerLock_.store(false, std::memory_order_release);
            return false;
        }
        
        buffer_[currentTail] = std::move(item);
        tail_.store(nextTail, std::memory_order_release);
        producerLock_.store(false, std::memory_order_release);
        return true;
    }
    
    /**
     * @brief Pop an item (single consumer only!).
     * @return Item if available, std::nullopt if empty.
     */
    std::optional<T> pop() {
        const size_t currentHead = head_.load(std::memory_order_relaxed);
        
        if (currentHead == tail_.load(std::memory_order_acquire)) {
            return std::nullopt;
        }
        
        T item = std::move(buffer_[currentHead]);
        head_.store((currentHead + 1) & (Capacity - 1), std::memory_order_release);
        return item;
    }
    
    /**
     * @brief Check if queue is empty.
     * @return true if empty at time of check.
     */
    bool empty() const {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief Get approximate queue size.
     * @return Number of items.
     */
    size_t size() const {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        return (tail - head + Capacity) & (Capacity - 1);
    }
    
private:
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
    alignas(64) std::atomic<bool> producerLock_;
    alignas(64) std::array<T, Capacity> buffer_;
};

/** @name Queue Size Constants */
///@{
constexpr size_t INBOUND_QUEUE_SIZE = 65536;   ///< Inbound queue capacity (64K)
constexpr size_t OUTBOUND_QUEUE_SIZE = 65536;  ///< Outbound queue capacity (64K)
constexpr size_t FEED_QUEUE_SIZE = 16384;      ///< Feed queue capacity (16K)
///@}

#endif // LOCKFREEQUEUE_H
