#ifndef BOT_CHAT_QUEUE_H
#define BOT_CHAT_QUEUE_H

#include "ObjectGuid.h"
#include <string>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>

struct BotChatRequest
{
    ObjectGuid botGuid;
    std::string prompt;
    uint32 queuedAt;
};

struct BotChatReply
{
    ObjectGuid botGuid;
    std::string text;
	uint32 deliverAt;
};

class BotChatQueue
{
public:
    static BotChatQueue& Instance();

    void Start();
    void Stop();
    bool Enqueue(ObjectGuid botGuid, std::string const& prompt);
    bool PopReply(BotChatReply& out);

private:
    void WorkerLoop();

    std::deque<BotChatRequest> m_requests;
    std::deque<BotChatReply> m_replies;
    std::mutex m_requestMutex;
    std::mutex m_replyMutex;
    std::thread m_worker;
    std::atomic<bool> m_running{false};

    static uint32 const MAX_PENDING = 8;
    static uint32 const MAX_AGE_SECONDS = 20;
};

#define sBotChatQueue BotChatQueue::Instance()

#endif