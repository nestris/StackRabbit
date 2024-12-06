#define CROW_USE_ASIO

#include "main.cpp"

#include "crow_all.h"
#include <future>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

struct RequestParams {
    std::string boardString;
    std::string secondBoardString;
    int level;
    int lines;
    int currentPiece;
    int nextPiece;
    std::string inputFrameTimeline;
    int playoutCount;
    int playoutLength;
    int pruningBreadth;
};

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


std::string httpGetTopMovesHybrid(std::string inputStr) {
    const char* cInputStr = inputStr.c_str();
    return mainProcess(cInputStr, GET_TOP_MOVES_HYBRID);
}

std::string httpRateMove(std::string inputStr) {
    const char* cInputStr = inputStr.c_str();
    return mainProcess(cInputStr, RATE_MOVE);
}

int getIntParam(const crow::request& req, const std::string& key, std::optional<int> defaultVal = std::nullopt) {
    const char * val = req.url_params.get(key);

    if (val == nullptr) {
        // If default value is provided, return it
        if (defaultVal.has_value()) return defaultVal.value();
        // Otherwise, throw an error
        throw std::runtime_error("Missing required parameter: " + key);
    }
    return std::stoi(val);
}

std::string getStringParam(const crow::request& req, const std::string& key, std::optional<std::string> defaultVal = std::nullopt) {
    const char * val = req.url_params.get(key);

    if (val == nullptr) {
        // If default value is provided, return it
        if (defaultVal.has_value()) return defaultVal.value();
        // Otherwise, throw an error
        throw std::runtime_error("Missing required parameter: " + key);
    }
    return std::string(val);
}

RequestParams getParamsFromRequest(const crow::request& req, bool requireSecondBoard) {
    auto url_params = req.url_params;
    RequestParams params;

    params.boardString = getStringParam(req, "board");
    params.level = getIntParam(req, "level", 18);
    params.lines = getIntParam(req, "lines", 0);
    params.currentPiece = getIntParam(req, "currentPiece", -1);
    params.nextPiece = getIntParam(req, "nextPiece", -1);
    params.inputFrameTimeline = getStringParam(req, "inputFrameTimeline", "X."); // 30hz default
    params.playoutCount = getIntParam(req, "playoutCount", 343); // depth 3 default
    params.playoutLength = getIntParam(req, "playoutLength", 3);
    params.pruningBreadth = getIntParam(req, "pruningBreadth", 25);

    // Assert board string is 200 characters long and only contains 0s and 1s
    if (params.boardString.length() != 200) {
        throw std::runtime_error("Board string must be 200 characters long");
    }
    for (char c : params.boardString) {
        if (c != '0' && c != '1') {
            throw std::runtime_error("Board string must only contain 0s and 1s");
        }
    }

    // If requireSecondBoard is true, assert second board string is 200 characters long and only contains 0s and 1s
    if (requireSecondBoard) {
        params.secondBoardString = getStringParam(req, "secondBoard");
        if (params.secondBoardString.length() != 200) {
            throw std::runtime_error("Second board string must be 200 characters long");
        }
        for (char c : params.secondBoardString) {
            if (c != '0' && c != '1') {
                throw std::runtime_error("Second board string must only contain 0s and 1s");
            }
        }
    } else params.secondBoardString = "";

    // Assert current and next piece are between -1 and 6
    if (params.currentPiece < -1 || params.currentPiece > 6) {
        throw std::runtime_error("Current piece must be between -1 and 6");
    }
    if (params.nextPiece < -1 || params.nextPiece > 6) {
        throw std::runtime_error("Next piece must be between -1 and 6");
    }

    // inputFrameTimeline must be some combination of 'X' and '.'
    for (char c : params.inputFrameTimeline) {
        if (c != 'X' && c != '.') {
            throw std::runtime_error("inputFrameTimeline must only contain 'X' and '.'");
        }
    }

    // Level must be 18+
    if (params.level < 18) {
        throw std::runtime_error("Level must be 18 or higher");
    }

    // Lines, playoutCount, playoutLength, and pruningBreadth must be positive
    if (params.lines < 0) {
        throw std::runtime_error("Lines must be 0 or higher");
    }
    if (params.playoutCount < 0) {
        throw std::runtime_error("Playout count must be 0 or higher");
    }
    if (params.playoutLength < 0) {
        throw std::runtime_error("Playout length must be 0 or higher");
    }
    if (params.pruningBreadth < 0) {
        throw std::runtime_error("Pruning breadth must be 0 or higher");
    }

    return params;
}

std::string generateRequestString(const RequestParams& params) {
    std::string requestString = params.boardString + "|";
    if (params.secondBoardString != "") {
        requestString += params.secondBoardString + "|";
    }
    requestString += std::to_string(params.level) + "|";
    requestString += std::to_string(params.lines) + "|";
    requestString += std::to_string(params.currentPiece) + "|";
    requestString += std::to_string(params.nextPiece) + "|";
    requestString += params.inputFrameTimeline + "|";
    requestString += std::to_string(params.playoutCount) + "|";
    requestString += std::to_string(params.playoutLength) + "|";
    requestString += std::to_string(params.pruningBreadth) + "|";

    return requestString;
}

int main() {
    crow::SimpleApp app;

    // Create a thread pool with a fixed number of threads
    ThreadPool pool(std::thread::hardware_concurrency());

    CROW_ROUTE(app, "/ping")
        .methods("GET"_method)([&pool](const crow::request& req) {
            return crow::response("pong");
        });

    
    // GET top-moves-hybrid/
    CROW_ROUTE(app, "/top-moves-hybrid")
        .methods("GET"_method)([&pool](const crow::request& req) {
            try {
                RequestParams params = getParamsFromRequest(req, false);
                std::string inputStr = generateRequestString(params);
                auto res = pool.enqueue([inputStr] {
                    return httpGetTopMovesHybrid(inputStr);
                });
                return crow::response(res.get());
            } catch (const std::exception& e) {
                return crow::response(400, e.what());
            } catch (...) {
                return crow::response(500, "An unknown error occurred");
            }
        });

    // GET rate-move/
    CROW_ROUTE(app, "/rate-move")
        .methods("GET"_method)([&pool](const crow::request& req) {
            try {
                RequestParams params = getParamsFromRequest(req, true);
                std::string inputStr = generateRequestString(params);
                auto res = pool.enqueue([inputStr] {
                    return httpRateMove(inputStr);
                });
                return crow::response(res.get());
            } catch (const std::exception& e) {
                return crow::response(400, e.what());
            } catch (...) {
                return crow::response(500, "An unknown error occurred");
            }
        });


    app.port(4500).multithreaded().run();
    return 0;
}

