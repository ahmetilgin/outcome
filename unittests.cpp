/* unittests.cpp
Unit testing for memory transactions
(C) 2013-2014 Niall Douglas http://www.nedproductions.biz/


Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#include "spinlock.hpp"
#include "timing.h"

#include <stdio.h>
#include <unordered_map>

#ifndef BOOST_MEMORY_TRANSACTIONS_DISABLE_CATCH
#define CATCH_CONFIG_RUNNER
#include "catch.hpp"
#endif

#include <mutex>
using namespace std;

TEST_CASE("spinlock/works", "Tests that the spinlock works as intended")
{
  boost::spinlock::spinlock<bool> lock;
  REQUIRE(lock.try_lock());
  REQUIRE(!lock.try_lock());
  lock.unlock();
  
  lock_guard<decltype(lock)> h(lock);
  REQUIRE(!lock.try_lock());
}

#ifdef _OPENMP
TEST_CASE("spinlock/works_threaded", "Tests that the spinlock works as intended under threads")
{
  boost::spinlock::spinlock<bool> lock;
  boost::spinlock::atomic<size_t> gate(0);
#pragma omp parallel
  {
    ++gate;
  }
  size_t threads=gate;
  for(size_t i=0; i<1000; i++)
  {
    gate.store(threads);
    size_t locked=0;
#pragma omp parallel for reduction(+:locked)
    for(int n=0; n<threads; n++)
    {
      --gate;
      while(gate);
      locked+=lock.try_lock();
    }
    REQUIRE(locked==1);
    lock.unlock();
  }
}

TEST_CASE("spinlock/works_transacted", "Tests that the spinlock works as intended under transactions")
{
  boost::spinlock::spinlock<bool> lock;
  boost::spinlock::atomic<size_t> gate(0);
  size_t locked=0;
#pragma omp parallel
  {
    ++gate;
  }
  size_t threads=gate;
#pragma omp parallel for
  for(int i=0; i<1000*threads; i++)
  {
    BOOST_BEGIN_TRANSACT_LOCK(lock)
    {
      ++locked;
    }
    BOOST_END_TRANSACT_LOCK(lock)
  }
  REQUIRE(locked==1000*threads);
}

static double CalculatePerformance(bool use_transact)
{
  boost::spinlock::spinlock<bool> lock;
  boost::spinlock::atomic<size_t> gate(0);
  struct
  {
    size_t value;
    char padding[64-sizeof(size_t)];
  } count[64];
  memset(&count, 0, sizeof(count));
  usCount start, end;
#pragma omp parallel
  {
    ++gate;
  }
  size_t threads=gate;
  //printf("There are %u threads in this CPU\n", (unsigned) threads);
  start=GetUsCount();
#pragma omp parallel for
  for(int thread=0; thread<threads; thread++)
  {
    --gate;
    while(gate);
    for(size_t n=0; n<10000000; n++)
    {
      if(use_transact)
      {
        BOOST_BEGIN_TRANSACT_LOCK(lock)
        {
          ++count[thread].value;
        }
        BOOST_END_TRANSACT_LOCK(lock)
      }
      else
      {
        std::lock_guard<decltype(lock)> g(lock);
        ++count[thread].value;      
      }
    }
  }
  end=GetUsCount();
  size_t increments=0;
  for(size_t thread=0; thread<threads; thread++)
  {
    REQUIRE(count[thread].value == 10000000);
    increments+=count[thread].value;
  }
  return increments/((end-start)/1000000000000.0);
}

TEST_CASE("performance/spinlock", "Tests the performance of spinlocks")
{
  printf("\n=== Spinlock performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculatePerformance(false));
  printf("2. Achieved %lf transactions per second\n", CalculatePerformance(false));
  printf("3. Achieved %lf transactions per second\n", CalculatePerformance(false));
}

TEST_CASE("performance/transaction", "Tests the performance of spinlock transactions")
{
  printf("\n=== Transacted spinlock performance ===\n");
  printf("This CPU %s support Intel TSX memory transactions.\n", boost::spinlock::intel_stuff::have_intel_tsx_support() ? "DOES" : "does NOT");
  printf("1. Achieved %lf transactions per second\n", CalculatePerformance(true));
  printf("2. Achieved %lf transactions per second\n", CalculatePerformance(true));
  printf("3. Achieved %lf transactions per second\n", CalculatePerformance(true));
#ifdef BOOST_USING_INTEL_TSX
  if(boost::spinlock::intel_stuff::have_intel_tsx_support())
  {
    printf("\nForcing Intel TSX support off ...\n");
    boost::spinlock::intel_stuff::have_intel_tsx_support_result=1;
    printf("1. Achieved %lf transactions per second\n", CalculatePerformance(true));
    printf("2. Achieved %lf transactions per second\n", CalculatePerformance(true));
    printf("3. Achieved %lf transactions per second\n", CalculatePerformance(true));
    boost::spinlock::intel_stuff::have_intel_tsx_support_result=0;
  }
#endif
}

static double CalculateMallocPerformance(size_t size, bool use_transact)
{
  boost::spinlock::spinlock<bool> lock;
  boost::spinlock::atomic<size_t> gate(0);
  usCount start, end;
#pragma omp parallel
  {
    ++gate;
  }
  size_t threads=gate;
  //printf("There are %u threads in this CPU\n", (unsigned) threads);
  start=GetUsCount();
#pragma omp parallel for
  for(int n=0; n<10000000*threads; n++)
  {
    void *p;
    if(use_transact)
    {
      BOOST_BEGIN_TRANSACT_LOCK(lock)
      {
        p=malloc(size);
      }
      BOOST_END_TRANSACT_LOCK(lock)
    }
    else
    {
      std::lock_guard<decltype(lock)> g(lock);
      p=malloc(size);
    }
    if(use_transact)
    {
      BOOST_BEGIN_TRANSACT_LOCK(lock)
      {
        free(p);
      }
      BOOST_END_TRANSACT_LOCK(lock)
    }
    else
    {
      std::lock_guard<decltype(lock)> g(lock);
      free(p);
    }
  }
  end=GetUsCount();
  REQUIRE(true);
//  printf("size=%u\n", (unsigned) map.size());
  return threads*10000000/((end-start)/1000000000000.0);
}

TEST_CASE("performance/malloc/transact/small", "Tests the transact performance of multiple threads using small memory allocations")
{
  printf("\n=== Small malloc transact performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateMallocPerformance(16, 1));
  printf("2. Achieved %lf transactions per second\n", CalculateMallocPerformance(16, 1));
  printf("3. Achieved %lf transactions per second\n", CalculateMallocPerformance(16, 1));
}

TEST_CASE("performance/malloc/transact/large", "Tests the transact performance of multiple threads using large memory allocations")
{
  printf("\n=== Large malloc transact performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateMallocPerformance(65536, 1));
  printf("2. Achieved %lf transactions per second\n", CalculateMallocPerformance(65536, 1));
  printf("3. Achieved %lf transactions per second\n", CalculateMallocPerformance(65536, 1));
}

static double CalculateUnorderedMapPerformance(size_t reserve, bool use_transact, bool readwrites)
{
  boost::spinlock::spinlock<bool> lock;
  boost::spinlock::atomic<size_t> gate(0);
  std::unordered_map<int, int> map;
  usCount start, end;
  if(reserve)
  {
    map.reserve(reserve);
    for(size_t n=0; n<reserve/2; n++)
      map.insert(std::make_pair(reserve+n, n));
  }
#pragma omp parallel
  {
    ++gate;
  }
  size_t threads=gate;
  //printf("There are %u threads in this CPU\n", (unsigned) threads);
  start=GetUsCount();
#pragma omp parallel for
  for(int thread=0; thread<threads; thread++)
  for(int n=0; n<10000000; n++)
  {
    if(readwrites)
    {
      // One thread always writes with lock, remaining threads read with transact
      bool amMaster=(thread==0);
      if(amMaster)
      {
        bool doInsert=((n/threads) & 1)!=0;
        std::lock_guard<decltype(lock)> g(lock);
        if(doInsert)
          map.insert(std::make_pair(n, n));
        else if(!map.empty())
          map.erase(map.begin());
      }
      else
      {
        if(use_transact)
        {
          BOOST_BEGIN_TRANSACT_LOCK(lock)
          {
            map.find(n-1);
          }
          BOOST_END_TRANSACT_LOCK(lock)
        }
        else
        {
          std::lock_guard<decltype(lock)> g(lock);
          map.find(n-1);
        }
      }
    }
    else
    {
      if(use_transact)
      {
        BOOST_BEGIN_TRANSACT_LOCK(lock)
        {
          if((n & 255)<128)
            map.insert(std::make_pair(n, n));
          else if(!map.empty())
            map.erase(map.begin());
        }
        BOOST_END_TRANSACT_LOCK(lock)
      }
      else
      {
        std::lock_guard<decltype(lock)> g(lock);
        if((n & 255)<128)
          map.insert(std::make_pair(n, n));
        else if(!map.empty())
          map.erase(map.begin());
      }
    }
  }
  end=GetUsCount();
  REQUIRE(true);
//  printf("size=%u\n", (unsigned) map.size());
  return threads*10000000/((end-start)/1000000000000.0);
}

TEST_CASE("performance/unordered_map/small", "Tests the performance of multiple threads using a small unordered_map")
{
  printf("\n=== Small unordered_map spinlock performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(0, false, false));
  printf("2. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(0, false, false));
  printf("3. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(0, false, false));
}

TEST_CASE("performance/unordered_map/large", "Tests the performance of multiple threads using a large unordered_map")
{
  printf("\n=== Large unordered_map spinlock performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, false, false));
  printf("2. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, false, false));
  printf("3. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, false, false));
}

TEST_CASE("performance/unordered_map2/large", "Tests the transact performance of multiple threads using a large unordered_map")
{
  printf("\n=== Large unordered_map transact performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, false, true));
#ifndef BOOST_HAVE_TRANSACTIONAL_MEMORY_COMPILER
  printf("2. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, false, true));
  printf("3. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, false, true));
#endif
}

TEST_CASE("performance/unordered_map/transact/small", "Tests the transact performance of multiple threads using a small unordered_map")
{
  printf("\n=== Small unordered_map transact performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(0, true, false));
#ifndef BOOST_HAVE_TRANSACTIONAL_MEMORY_COMPILER
  printf("2. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(0, true, false));
  printf("3. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(0, true, false));
#endif
}

TEST_CASE("performance/unordered_map/transact/large", "Tests the transact performance of multiple threads using a large unordered_map")
{
  printf("\n=== Large unordered_map transact performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, true, false));
#ifndef BOOST_HAVE_TRANSACTIONAL_MEMORY_COMPILER
  printf("2. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, true, false));
  printf("3. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, true, false));
#endif
}

TEST_CASE("performance/unordered_map2/transact/large", "Tests the transact performance of multiple threads using a large unordered_map")
{
  printf("\n=== Large unordered_map transact performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, true, true));
#ifndef BOOST_HAVE_TRANSACTIONAL_MEMORY_COMPILER
  printf("2. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, true, true));
  printf("3. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, true, true));
#endif
}
#endif

#ifndef BOOST_MEMORY_TRANSACTIONS_DISABLE_CATCH
int main(int argc, char *argv[])
{
#ifdef _OPENMP
  printf("These unit tests have been compiled with parallel support. I will use as many threads as CPU cores.\n");
#else
  printf("These unit tests have not been compiled with parallel support and will execute only those which are sequential.\n");
#endif
#ifdef BOOST_HAVE_TRANSACTIONAL_MEMORY_COMPILER
  printf("These unit tests have been compiled using a transactional compiler. I will use __transaction_relaxed.\n");
#else
  printf("These unit tests have not been compiled using a transactional compiler.\n");
#endif
  int result=Catch::Session().run(argc, argv);
  return result;
}
#endif
