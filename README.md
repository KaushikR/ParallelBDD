ParallelBDD
===========

A parallel ROBDD implementation where each thread builds it's own ROBDD with a different variable ordering than the other.
Overall idea is to find the best variable ordering which uses the least number of nodes. 

Requires gcc 4.8 or higher with support for POSIX standard.
To compile on mingw: g++ bdd.cpp -std=gnu++11 -pthread
To run : <a> <ckt> <No. of Threads> <Runs per thread>

NOTE : Each thread works shuffles 5 Inputs. 
       So, if there are only 10 inputs, only 2 threads are needed. 
       15 inputs - 3 threads.
       20 inputs - 4 threads and so on.
       
Both the HashTables are shared between all threads. A fine grain lock is used, i.e a lock for every bucket.
