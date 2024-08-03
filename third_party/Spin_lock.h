#pragma once
#include <atomic>

class Spin_lock 
{
    std::atomic_bool locked = false ;
public:
    void lock() {
        while (locked.exchange(true, std::memory_order_acquire)) { ; }
    }
    void unlock() {
        locked.exchange(false, std::memory_order_release);
    }
};