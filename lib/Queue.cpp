#include "./Queue.h"

void Queue::push(string json)
{
    mutex_lock.lock();
    json_queue.push(json);
    mutex_lock.unlock();
}

string Queue::front()
{    
    if (json_queue.empty() != true)
    {
        mutex_lock.lock();
            string data = json_queue.front();
            json_queue.pop();
        mutex_lock.unlock();

        return data;
    }
    else
    {
        return NULL;
    }
}

int Queue::size()
{
    return json_queue.size();
}

bool Queue::empty()
{
    return json_queue.empty();
}
