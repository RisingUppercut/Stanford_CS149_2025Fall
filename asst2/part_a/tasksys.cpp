#include "tasksys.h"

IRunnable::~IRunnable() {}

ITaskSystem::ITaskSystem(int num_threads) {}
ITaskSystem::~ITaskSystem() {}

/*
 * ================================================================
 * Serial task system implementation
 * ================================================================
 */

const char* TaskSystemSerial::name() {
    return "Serial";
}

TaskSystemSerial::TaskSystemSerial(int num_threads): ITaskSystem(num_threads) {
}

TaskSystemSerial::~TaskSystemSerial() {}

void TaskSystemSerial::run(IRunnable* runnable, int num_total_tasks) {
    for (int i = 0; i < num_total_tasks; i++) {
        runnable->runTask(i, num_total_tasks);
    }
}

TaskID TaskSystemSerial::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                          const std::vector<TaskID>& deps) {
    // You do not need to implement this method.
    return 0;
}

void TaskSystemSerial::sync() {
    // You do not need to implement this method.
    return;
}

/*
 * ================================================================
 * Parallel Task System Implementation
 * ================================================================
 */

const char* TaskSystemParallelSpawn::name() {
    return "Parallel + Always Spawn";
}

TaskSystemParallelSpawn::TaskSystemParallelSpawn(int num_threads): ITaskSystem(num_threads) {
    //
    // TODO: CS149 student implementations may decide to perform setup
    // operations (such as thread pool construction) here.
    // Implementations are free to add new class member variables
    // (requiring changes to tasksys.h).
    //
    max_threads = num_threads;
}

TaskSystemParallelSpawn::~TaskSystemParallelSpawn() {}

void TaskSystemParallelSpawn::run(IRunnable* runnable, int num_total_tasks) {


    //
    // TODO: CS149 students will modify the implementation of this
    // method in Part A.  The implementation provided below runs all
    // tasks sequentially on the calling thread.
    //
    int num_workers = std::min(num_total_tasks, max_threads);

    std::vector<std::thread> workers;
    std::atomic<int> next_task(0); 
    auto worker = [&]() {
        while (true) {
            int id = next_task.fetch_add(1);
            if (id >= num_total_tasks) {
                break;
            }
            runnable->runTask(id, num_total_tasks);
        }
    };
    for (int i = 0; i < num_workers; i++) {
        workers.emplace_back(worker);
    }
    
    for (auto& t : workers) {
        t.join();
    }
}

TaskID TaskSystemParallelSpawn::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                 const std::vector<TaskID>& deps) {
    // You do not need to implement this method.
    return 0;
}

void TaskSystemParallelSpawn::sync() {
    // You do not need to implement this method.
    return;
}

/*
 * ================================================================
 * Parallel Thread Pool Spinning Task System Implementation
 * ================================================================
 */

const char* TaskSystemParallelThreadPoolSpinning::name() {
    return "Parallel + Thread Pool + Spin";
}

TaskSystemParallelThreadPoolSpinning::TaskSystemParallelThreadPoolSpinning(int num_threads): ITaskSystem(num_threads) {
    //
    // TODO: CS149 student implementations may decide to perform setup
    // operations (such as thread pool construction) here.
    // Implementations are free to add new class member variables
    // (requiring changes to tasksys.h).
    //
    max_threads = num_threads;
    ready.store(false); 
    shutdown = false;

    auto worker = [&]() {
        while (!shutdown) {
            if (ready.load() && next_task_id.load() < num_tasks) {
                int task_id = next_task_id.fetch_add(1);
                if (task_id < num_tasks) {
                    cur_runnable->runTask(task_id, num_tasks);
                    num_tasks_completed.fetch_add(1);
                }
            } else {
                std::this_thread::yield();
            }
        } 
        
    };

    for (int i = 0; i < num_threads; i++) {
        workers.emplace_back(std::thread(worker));
    }
}

TaskSystemParallelThreadPoolSpinning::~TaskSystemParallelThreadPoolSpinning() {
    shutdown = true;
    for (int i = 0; i < max_threads; i++) {
        workers[i].join();
    }
}

void TaskSystemParallelThreadPoolSpinning::run(IRunnable* runnable, int num_total_tasks) {


    //
    // TODO: CS149 students will modify the implementation of this
    // method in Part A.  The implementation provided below runs all
    // tasks sequentially on the calling thread.
    //
    
    cur_runnable = runnable;
    num_tasks = num_total_tasks;
    num_tasks_completed.store(0);
    next_task_id.store(0);
    ready.store(true);

    // int task_id;  // Instead of just spinning, the main thread could also be a worker
    // while ((task_id = next_task_id.fetch_add(1)) < num_total_tasks) {
    //     runnable->runTask(task_id, num_total_tasks);
    //     num_tasks_completed.fetch_add(1);
    // }
    while (num_tasks_completed.load() < num_tasks) {
        std::this_thread::yield();
    }
    ready.store(false);
}

TaskID TaskSystemParallelThreadPoolSpinning::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                              const std::vector<TaskID>& deps) {
    // You do not need to implement this method.
    return 0;
}

void TaskSystemParallelThreadPoolSpinning::sync() {
    // You do not need to implement this method.
    return;
}

/*
 * ================================================================
 * Parallel Thread Pool Sleeping Task System Implementation
 * ================================================================
 */

const char* TaskSystemParallelThreadPoolSleeping::name() {
    return "Parallel + Thread Pool + Sleep";
}

TaskSystemParallelThreadPoolSleeping::TaskSystemParallelThreadPoolSleeping(int num_threads): ITaskSystem(num_threads) {
    //
    // TODO: CS149 student implementations may decide to perform setup
    // operations (such as thread pool construction) here.
    // Implementations are free to add new class member variables
    // (requiring changes to tasksys.h).
    //
    max_threads = num_threads;
    shutdown = false;
    auto worker = [&]() {
        while (!shutdown) {
            bool should_wake = false;
            std::unique_lock<std::mutex> lck(_mutex);
            while (!(!ready_queue.empty() || shutdown)) {
                worker_cv.wait(lck);
            }
            if (shutdown) {
                break;
            }
            bulk_task* bt = ready_queue.front();
            int task_id = bt->next_task_id.fetch_add(1);
            if (task_id == bt->num_tasks - 1) {
                ready_queue.pop();
            }
            lck.unlock();
            bt->cur_runnable->runTask(task_id, bt->num_tasks);
            
            int num = bt->task_completed.fetch_add(1);
            // bt->task_completed.fetch_add(1);
            if (num == bt->num_tasks - 1) {
                lck.lock();
                for (bulk_task* t : dep_on[bt->bulk_task_id]) {
                    t->deps_remain--;
                    if (t->deps_remain == 0) {
                        ready_queue.push(t);
                        // worker_cv.notify_all();
                        should_wake = true;
                    }
                }
                tasks_done.insert(bt->bulk_task_id);
                // main_cv.notify_all();
                lck.unlock();
            }
            if (should_wake) {
                worker_cv.notify_all();
            }
            main_cv.notify_all();

        } 
    };

    for (int i = 0; i < num_threads; i++) {
        workers.emplace_back(std::thread(worker));
    }
}

TaskSystemParallelThreadPoolSleeping::~TaskSystemParallelThreadPoolSleeping() {
    //
    // TODO: CS149 student implementations may decide to perform cleanup
    // operations (such as thread pool shutdown construction) here.
    // Implementations are free to add new class member variables
    // (requiring changes to tasksys.h).
    //
    {
        std::lock_guard<std::mutex> lock(_mutex);
        shutdown = true;
    }
    worker_cv.notify_all(); 
    for (auto& t : workers) {
        t.join();
    }

}

void TaskSystemParallelThreadPoolSleeping::run(IRunnable* runnable, int num_total_tasks) {


    //
    // TODO: CS149 students will modify the implementation of this
    // method in Parts A and B.  The implementation provided below runs all
    // tasks sequentially on the calling thread.
    //

    runAsyncWithDeps(runnable, num_total_tasks, {});
    sync();
}

TaskID TaskSystemParallelThreadPoolSleeping::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                    const std::vector<TaskID>& deps) {


    //
    // TODO: CS149 students will implement this method in Part B.
    //
    TaskID ret;
    bool should_wake = false;
    {
    std::lock_guard<std::mutex> lck(_mutex);
    bulk_task* bt = new bulk_task;
    bt->bulk_task_id = next_bulk_task_id++;
    bt->cur_runnable = runnable;
    bt->num_tasks = num_total_tasks;
    bt->deps_remain = 0;
    bt->next_task_id.store(0);
    bt->task_completed.store(0);
    for (auto& id : deps) {
        if (!tasks_done.count(id)) {
            dep_on[id].push_back(bt);
            bt->deps_remain++;
        }
    }
  
    if (bt->deps_remain == 0) {
        ready_queue.push(bt);
        should_wake = true;
    }
    ret = bt->bulk_task_id;
    all_tasks.push_back(bt);
    }   
    if (should_wake) {
        worker_cv.notify_all();
    }
    return ret;
}

void TaskSystemParallelThreadPoolSleeping::sync() {

    //
    // TODO: CS149 students will modify the implementation of this method in Part B.
    //

    std::unique_lock<std::mutex> lck(_mutex); 
    while (tasks_done.size() != all_tasks.size()) {
        main_cv.wait(lck);
    }

    for (bulk_task* bt : all_tasks) {
        delete bt;
    }

    tasks_done.clear(); // TODo FIx
    all_tasks.clear();
    dep_on.clear();
    ready_queue = {};
    return;
}
