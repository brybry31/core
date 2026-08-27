#include "BotChatQueue.h"
#include <curl/curl.h>
#include "../../../dep/json/json.hpp"
#include <chrono>

static uint32 NowSeconds()
{
    return (uint32)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

static size_t BotChatWriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

BotChatQueue& BotChatQueue::Instance()
{
    static BotChatQueue instance;
    return instance;
}

void BotChatQueue::Start()
{
    if (m_running)
        return;
    m_running = true;
    m_worker = std::thread(&BotChatQueue::WorkerLoop, this);
}

void BotChatQueue::Stop()
{
    m_running = false;
    if (m_worker.joinable())
        m_worker.join();
}

bool BotChatQueue::Enqueue(ObjectGuid botGuid, std::string const& prompt)
{
	Start();
    std::lock_guard<std::mutex> lock(m_requestMutex);
    if (m_requests.size() >= MAX_PENDING)
        return false;
    BotChatRequest req;
    req.botGuid = botGuid;
    req.prompt = prompt;
    req.queuedAt = NowSeconds();
    m_requests.push_back(req);
    return true;
}

bool BotChatQueue::PopReply(BotChatReply& out)
{
    std::lock_guard<std::mutex> lock(m_replyMutex);
    uint32 now = NowSeconds();
    for (auto it = m_replies.begin(); it != m_replies.end(); ++it)
    {
        if (it->deliverAt <= now)
        {
            out = *it;
            m_replies.erase(it);
            return true;
        }
    }
    return false;
}

void BotChatQueue::WorkerLoop()
{
    while (m_running)
    {
        BotChatRequest req;
        bool haveWork = false;
        {
            std::lock_guard<std::mutex> lock(m_requestMutex);
            while (!m_requests.empty())
            {
                req = m_requests.front();
                m_requests.pop_front();
                if (NowSeconds() - req.queuedAt <= MAX_AGE_SECONDS)
                {
                    haveWork = true;
                    break;
                }
            }
        }

        if (!haveWork)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        CURL* curl = curl_easy_init();
        if (!curl)
            continue;

        nlohmann::json j;
        j["model"] = "llama3.1:8b";
        j["prompt"] = req.prompt;
        j["stream"] = false;
        j["options"]["num_predict"] = 60;
        std::string body = j.dump();

        std::string result;
        curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:11434/api/generate");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, BotChatWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK)
            continue;

        try
        {
            auto parsed = nlohmann::json::parse(result);
            std::string text = parsed["response"];
            while (!text.empty() && (text.front() == '"' || text.front() == '\'' || text.front() == '\n' || text.front() == ' '))
                text.erase(0, 1);
            while (!text.empty() && (text.back() == '"' || text.back() == '\'' || text.back() == '\n' || text.back() == ' '))
                text.pop_back();
            if (text.size() > 250)
                text = text.substr(0, 250);
            if (text.empty())
                continue;

            BotChatReply reply;
            reply.botGuid = req.botGuid;
            reply.text = text;
            reply.deliverAt = NowSeconds() + 2 + (rand() % 11);
            std::lock_guard<std::mutex> lock(m_replyMutex);
            m_replies.push_back(reply);
        }
        catch (...)
        {
        }
    }
}