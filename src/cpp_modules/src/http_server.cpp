#define CROW_USE_ASIO
#include "crow_all.h"
#include <future>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

// Thread pool implementation
class ThreadPool {
public:
    ThreadPool(size_t threads) : stop(false) {
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex);
                        condition.wait(lock, [this] { return stop || !tasks.empty(); });
                        if (stop && tasks.empty())
                            return;
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> {
        using return_type = typename std::result_of<F(Args...)>::type;
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");
            tasks.emplace([task]() { (*task)(); });
        }
        condition.notify_one();
        return res;
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread& worker : workers)
            worker.join();
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

// Expensive computation function
std::string expensive_function(const std::string& input) {
    std::this_thread::sleep_for(std::chrono::seconds(5)); // Simulate expensive computation
    return "Processed: " + input;
}

int main() {
    crow::SimpleApp app;

    // Create a thread pool with a fixed number of threads
    ThreadPool pool(std::thread::hardware_concurrency());

    CROW_ROUTE(app, "/expensive")
        .methods("GET"_method)([&pool](const crow::request& req) {
            auto future_result = pool.enqueue(expensive_function, req.body);
            return crow::response(future_result.get());
        });

    app.port(4500).multithreaded().run();
    return 0;
}
