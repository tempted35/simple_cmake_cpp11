#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <deque>
#include <queue>
#include <future>
#include <mutex>

#include "boost/date_time/posix_time/posix_time.hpp"
#include "boost/filesystem.hpp"

using namespace std;


//=============================================================================
//
//=============================================================================
const int ITERS = 100;

std::mutex io_mutex;

class LogFile
{
private:
    std::mutex m_mu;
    std::once_flag m_flag;
    ofstream m_f;
public:
    LogFile()
    {
      
    }

    void shared_print(string id, int value)
    {
      /*
      std::unique_lock<mutex> locker2(m_muOpen);
      // Lazy initialization /initialize on first use
      if(!m_f.is_open())
      {
        m_f.open("log.txt");
      }
      */

      // File will be opened only once by one thread
      std::call_once(m_flag, [&](){ m_f.open("log.txt"); });

      std::unique_lock<mutex> locker(m_mu, std::defer_lock);
      m_f << "From " << id << ":" << value << endl;
    }
};


//=============================================================================
//
//=============================================================================
class Buffer
{
public:
  enum {BUF_SIZE = 10};
  Buffer() 
    : p(0), c(0), mFull(0)
  {}

  void put(int m)
  {
    // Monitors provide a mechanism for threads to temporarily give up
    // exclusive access in order to wait for some condition to be met,
    // before regaining exclusive access and resuming their task.
    std::unique_lock<mutex>  lock(m_mutex);
    if(mFull == BUF_SIZE)
    {
      {
        std::unique_lock<mutex>  lock(io_mutex);
        std::cout << "Buffer is full. Waiting..." << std::endl;
      }
      // The first time we execute the while loop condition after the above
      // "acquire", we are asking, "Does the condition/predicate/assertion we
      // are waiting for happen to already be true?
      while(mFull == BUF_SIZE)
      {
        // If this is not the first time the "while" condition is being 
        // checked, then we are asking the question, "Now that another thread
        // using this monitor has notified me and woken me up and I have been
        // context-switched back to, did the condition/predicate/assertion 
        // we are waiting stay true between the last time that I was woken up
        // and the time that I re-acquired the lock inside the "wait" call in 
        // the last iteration of this loop, or did some other thread cause the
        // condition to become false again in the meantime thus making this
        // a spurious wakeup?


        // 'wait()':
        // (1) Atomically:
        // (a) release the mutex m,
        // (b) move this thread from the "ready queue" to the condition 
        //     variable c's "wait-queue" (a.k.a "sleep-queue") of threads, and
        // (c) sleep this thread.  (Context is synchronously yielded to another thread
        //
        // (2) Once this thread is subsequently notified/signaled and resumed,
        //     then automatically re-acquire the mutex m.
        //
        // The atomicity of the operations within step 1 is important to avoid
        // race conditions that would be caused by a preempted thread switch
        // in-between them.  One failure mode that could occur if these were 
        // not atomic is a MISSED WAKEUP, in which the thread could be on c's
        // sleep-queue and have released the mutex, but a preempty thread switch
        // occurred before the thread went to sleep, and anothe thread called 
        // a signal/notify operation on c moving the first thread back out of c's
        // queue.  As soon as the first thread in question is switched back to,
        // its program counter will be at step 1c, and it will sleep and be unable
        // to be woken up again, violating the invariant that it should have been 
        // on c's sleep-queue when it slept.  
        //
        // As a design rule, multiple condition variables can be associated with 
        // the same mutex, but not vice-versa.  (This is a one-to-many correspondence).
        // This is beecause the predicate Pc is the same for all threads using the 
        // monitor and must be protected with mutual exclusion from other threads
        // that might cause the condition to be changed.
        m_cond.wait(lock);

        // Temporarily prevent any other thread on any core from doing
        // operations on m or cv.

        // release(m)  // Atomically release lock "m" so other code using this 
                       // concurrent data can operate move this thread to cv's
                       // wait-queue so that it will be notified sometime when
                       // the condition variable becomes true, and sleep this 
                       // thread.  Re-enable other threads and cores to do 
                       // operations on m and cv.
        //
        // Context switch occurs on this core.
        //
        // At some future time, the condition we are waiting for becomes true,
        // and another thread using this monitor (m, cv) does either a
        // signal/notify that happens to wake this thread up, or a notifyAll
        // that wakes us up, meaning that we have been taken out of cv's
        // wait-queue.
        //
        // During this time, other threads may be switched to that caused the
        // condition to become false again, or condition may toggle one or
        // more times, or it may happen to stay true.
        //
        // This thread is switched back to on some core.
        //
        // acquire(m)  // Lock "m" is re-acquired.
    
      // End this loop iteration and re-check the "while" loop condition to
      // make sure the predicate is still true.
      }
    }

    buf[p] = m;
    p = (p + 1) % BUF_SIZE;
    ++mFull;
    m_cond.notify_one();
  }

  int get()
  {
    std::unique_lock<mutex> lk(m_mutex);
    if(mFull == 0)
    {
      {
        std::unique_lock<mutex> lk1(io_mutex);
        std::cout << "Buffer is empty. Waiting..." << std::endl;
      }

      while(mFull == 0)
      {
        m_cond.wait(lk);
      }
    } 

    int i = buf[c];
    c = (c + 1) % BUF_SIZE;
    --mFull;
    m_cond.notify_one();
    return i;
  }

private:
  Buffer(const Buffer&);

   std::mutex m_mutex;

  // Conceptually a condition variable is a queue of threads, associated with
  // a monitor, on which a thread may wait for some condition to become true.
  // Thus each conditional variable C is associated with an assertion Pc.
  // While a thread is waiting on a condition variable, that thread is not
  // considered to occupy the monitor, and so other threads may enter the 
  // monitor to change the monitor's state.  These other threads may signal
  // the conditional variable C to indicate that assertion Pc is true in 
  // the current state.
  std::condition_variable m_cond;
  unsigned int p, c, mFull;
  int buf[BUF_SIZE];
};

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void writer(Buffer &buf)
{
  for(int n = 0; n < ITERS; ++n)
  {
    {
      std::lock_guard<mutex> lock(io_mutex);
      std::cout << "Sending: " << n << std::endl;
    }
    buf.put(n);
  }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void reader(Buffer &buf)
{
  for(int x = 0; x < ITERS; ++x)
  {
    int n = buf.get();
    {
      std::lock_guard<mutex> lock(io_mutex);
      std::cout << "Received: " << n << std::endl;
    }
    // Simulate a slower reader
    std::this_thread::sleep_for(chrono::milliseconds(100));
  }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void getTime()
{
  using namespace boost::posix_time;
  // ptime t1(boost::time_from_string("2002-01-20 23:59:59.000"))
  ptime t1(boost::gregorian::date(2002, 1, 1));
  std::cout << t1 << std::endl;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int main(int argc, char *argv[])
{
  if(argc == 2)
  {
    std::cout << boost::filesystem::is_directory(argv[1]) << std::endl;
  }

  getTime();

  Buffer buf;
  std::thread t1(writer, std::ref(buf));
  std::thread t2(reader, std::ref(buf));
  t1.join();
  t2.join();

  return 0;
}

/* Avoiding deadlock
1. Prefer locking a single mutext
2. Avoid locking a mutex and then calling a user defined function
3. Use std::lock() to lock more than one mutex.
4. Lock the mutexes in same order for all threads
  -- or hierarchy of mutexes where a thread holding a lower level
     mutex may not lock a higher level mutex
*/

