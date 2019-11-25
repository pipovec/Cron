#include "QUEUE.h"

Queue::push(string json)
{
    mutex_lock.lock();
    json_queue.push(json);
    mutex_lock.unlock();
}

Queue::front()
{    
    if (json_queue.empty() !== true)
    {
        mutex_lock.lock();
            string data = json_queue.front();
            json_queue.pop();
        mutex_lock.unlock();

        return data;
    }
    else
    {
        return null;
    }
}

Queue::size()
{
    return json_queue.size();
}

Queue::empty()
{
    return json_queue.empty();
}
