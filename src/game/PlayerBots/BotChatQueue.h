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
	uint8 chatType;
};

struct BotChatReply
{
    ObjectGuid botGuid;
    std::string text;
	uint32 deliverAt;
	uint8 chatType;
};

class BotChatQueue
{
public:
    static BotChatQueue& Instance();

    void Start();
    void Stop();
    bool Enqueue(ObjectGuid botGuid, std::string const& prompt, uint8 chatType);
	bool TryClaim(ObjectGuid speaker, std::string const& msg, uint32 now);
    bool PopReply(BotChatReply& out);

private:
    void WorkerLoop();

    std::deque<BotChatRequest> m_requests;
    std::deque<BotChatReply> m_replies;
    std::mutex m_requestMutex;
    std::mutex m_replyMutex;
    std::thread m_worker;
    std::atomic<bool> m_running{false};
	ObjectGuid m_lastClaimSpeaker;
    std::string m_lastClaimMsg;
    uint32 m_lastClaimTime = 0;
    std::mutex m_claimMutex;

    static uint32 const MAX_PENDING = 8;
    static uint32 const MAX_AGE_SECONDS = 20;
};

#define sBotChatQueue BotChatQueue::Instance()

#endif