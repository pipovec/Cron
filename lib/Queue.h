#ifndef QUEUE_H
#define QUEUE_H

#include <mutex>
#include <queue>
#include <iostream>

using namespace std;

/**
 * Jednoducha fronta na ziskane JSON-s. Aby sa dal paralelizovat program
 * 
 * pipovec 25.11.2019
 * 
*/
class Queue
{
public:    
    int size();
    string front();
    bool empty();
    bool push(string json);

private:
    queue<string> json_queue;
    mutex mutex_lock;    
}
#endif // QUEUE_H