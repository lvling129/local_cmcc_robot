#include "avvtn_capture.h"
#include "utils/Logger.hpp"
#include "ros2/ros_manager.hpp"
#include "utils/json.hpp"

void AvvtnCapture::aiuiCallback(void *user_data, const IAIUIEvent &event)
{
    AvvtnCapture *self = static_cast<AvvtnCapture *>(user_data);
    try
    {
        switch (event.getEventType())
        {
            // SDK状态
            case AIUIConstant::EVENT_STATE:
            {
                LOG_INFO("接收到AIUI SDK状态信息");
                switch (event.getArg1())
                {
                    case AIUIConstant::STATE_IDLE: 
                        std::cout << "EVENT_STATE: STATE_IDLE" << std::endl;
                        LOG_INFO("AIUI当前状态: IDLE");
                        break;
                    case AIUIConstant::STATE_READY:
                        std::cout << "EVENT_STATE: STATE_READY" << std::endl;
                        LOG_INFO("AIUI当前状态: READY");
                        break;
                    case AIUIConstant::STATE_WORKING:
                        std::cout << "EVENT_STATE: STATE_WORKING" << std::endl;
                        LOG_INFO("AIUI当前状态: WORKING");
                        break;
                }
            }
            break;

            // 唤醒事件
            case AIUIConstant::EVENT_WAKEUP:
            {
                LOG_INFO("接收到AIUI唤醒事件EVENT_WAKEUP: %s", event.getInfo());
                LOG_INFO("pcm播放器停止播放");
                std::cout << "EVENT_WAKEUP: " << event.getInfo() << std::endl;
                aiui_pcm_player_stop();

                //等待1S，优化用户体验
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                self->is_sleeping = false;
            }
            break;

            // 休眠事件
            case AIUIConstant::EVENT_SLEEP:
            {
                self->is_sleeping = true;
                LOG_INFO("接收到AIUI休眠事件EVENT_SLEEP: arg1 = %d", event.getArg1());
                std::cout << "EVENT_SLEEP: arg1=" << event.getArg1() << std::endl;
            }
            break;

            // VAD事件
            case AIUIConstant::EVENT_VAD:
            {
                LOG_DEBUG("接收到AIUI VAD事件");
                switch (event.getArg1())
                {
                    case AIUIConstant::VAD_BOS_TIMEOUT:
                        std::cout << "EVENT_VAD: VAD_BOS_TIMEOUT" << std::endl;
                        LOG_DEBUG("EVENT_VAD: VAD_BOS_TIMEOUT");
                        break;
                    case AIUIConstant::VAD_BOS:
                        std::cout << "EVENT_VAD: BOS" << std::endl;
                        LOG_DEBUG("EVENT_VAD: BOS");
                        break;
                    case AIUIConstant::VAD_EOS:
                        std::cout << "EVENT_VAD: EOS" << std::endl;
                        LOG_DEBUG("EVENT_VAD: EOS");
                        break;
                    case AIUIConstant::VAD_VOL:
                        //LOG_DEBUG("EVENT_VAD: vol = %d", event.getArg2());
                        // cout << "EVENT_VAD: vol=" << event.getArg2() << endl;
                        break;
                }
            }
            break;

            // 结果事件
            case AIUIConstant::EVENT_RESULT:
            {
                Json::Value bizParamJson;
                Json::Reader reader;
                if (!reader.parse(event.getInfo(), bizParamJson, false))
                {
                    LOG_ERROR("parse error! info = %s", event.getInfo());
                    std::cout << "parse error! info=" << event.getInfo() << std::endl;
                    break;
                }
                Json::Value &data    = (bizParamJson["data"])[0];
                Json::Value &params  = data["params"];
                Json::Value &content = (data["content"])[0];

                std::string sub = params["sub"].asString();
                std::string sid = event.getData()->getString("sid", "");

                if (sub == "iat")
                {
                    if (sid != self->current_iat_sid_)
                    {
                        LOG_DEBUG("iat**********************************");
                        LOG_DEBUG("sid = %s", sid.c_str());
                        std::cout << "iat**********************************" << std::endl;
                        std::cout << "sid=" << sid << std::endl;
                        self->current_iat_sid_ = sid;

                        LOG_DEBUG("新的会话，清空之前识别缓存，并且停止播放");
                        // 新的会话，清空之前识别缓存，并且停止播放
                        self->iat_text_buffer_.clear();
                        self->stream_nlp_answer_buffer_.clear();
                        self->aiui_wrapper_.listener_->tts_helper_ptr_->clear();
                        self->intent_cnt_ = 0;
                    }
                }
                else if (sub == "tts")
                {
                    if (sid != self->current_tts_sid_)
                    {
                        LOG_DEBUG("tts**********************************");
                        LOG_DEBUG("sid = %s", sid.c_str());
                        std::cout << "tts**********************************" << std::endl;
                        std::cout << "sid=" << sid << std::endl;
                        self->tts_len_         = 0;
                        self->current_tts_sid_ = sid;
                    }
                }
                int dataLen = 0;
                Json::Value empty;
                std::string cnt_id = content.get("cnt_id", empty).asString();
                // 注意：当buffer里存字符串时也不是以0结尾，当使用C语言时，转成字符串则需要自已在末尾加0
                const char *buffer = event.getData()->getBinary(cnt_id.c_str(), &dataLen);

                std::string resultStr = std::string(buffer, dataLen);
                if (sub != "tts")
                {
                    LOG_DEBUG("JSON原始数据: %s", resultStr.c_str());
                }
                // 当前仅解析iat tts nlp
                if (sub == "iat")
                {
                    LOG_DEBUG("接收到AIUI返回的语音识别结果iat");
                    LOG_DEBUG("%s: ", event.getInfo());

                    self->handleAiuiIat(reader, buffer, dataLen);
                    self->is_skill = false;
                    self->is_knowledge = false;
                }
                else if (sub == "tts")
                {
                    LOG_DEBUG("接收到AIUI返回的语音合成结果tts");
                    if (sid == self->ignore_tts_sid_)
                    {
                        LOG_INFO("ignore current tts");
                        break;
                    }
                    self->handleAiuiTts(reader, content, event, bizParamJson, buffer, dataLen);
                }
                else if (sub == "nlp")
                {
                    LOG_DEBUG("接收到AIUI返回的语义理解结果nlp");
                    LOG_DEBUG("%s: ", event.getInfo());
                    LOG_DEBUG("%s", resultStr.c_str());

                    self->handleAiuiStreamNlp(reader, buffer, dataLen);
                }
                else if (sub == "event")
                {
                    //服务事件
                    LOG_DEBUG("接收到AIUI返回的【服务事件event】");
                }
                else if (sub == "cbm_tidy")
                {
                    self->handleCbmTidy(resultStr);
                }
                else if (sub == "cbm_semantic")
                {
                    self->handleCbmSemantic(resultStr);
                }
                else if (sub == "cbm_tool_pk")
                {
                    self->handleCbmToolPk(resultStr);
                }
                else if (sub == "cbm_retrieval_classify")
                {
                    self->handleCbmRetrievalClassify(resultStr);
                }
                else if (sub == "cbm_plugin")
                {
                    //智能体
                    LOG_INFO("接收到AIUI返回的【智能体cbm_plugin】");
                }
                else if (sub == "cbm_knowledge")
                {
                    self->handleCbmKnowledge(resultStr);
                }
                else
                {
                    // 其他结果
                    //std::cout << sub << ": " << event.getInfo() << std::endl;
                    //std::cout << resultStr << std::endl;
                    
                    //LOG_INFO("除iat,tts,nlp外的其他返回数据:");
                    //LOG_INFO("%s: ", event.getInfo());
                    //LOG_INFO("%s", resultStr.c_str());
                }
            }
            break;

            // 命令返回事件
            case AIUIConstant::EVENT_CMD_RETURN:
            {
                LOG_INFO("AIUI执行完命令返回");
                LOG_INFO("CMD = %d, retCode = %d", event.getArg1(), event.getArg2());
                if (AIUIConstant::SUCCESS == event.getArg2())
                {
                    LOG_INFO("AIUI命令执行成功");
                }
                else
                {
                    LOG_ERROR("AIUI命令执行失败");
                }
                if (AIUIConstant::CMD_SYNC == event.getArg1())
                {
                    int retCode = event.getArg2();
                    if (AIUIConstant::SUCCESS == retCode)
                    {
                        std::string sid = event.getData()->getString("sid", "");
                        std::cout << "数据同步成功, sid=" << sid << std::endl;
                    }
                    else
                    {
                        std::cout << "数据同步失败, 错误码=" << retCode << std::endl;
                    }
                }
            }
            break;

            // 开始录音事件
            case AIUIConstant::EVENT_START_RECORD:
            {
                LOG_INFO("AIUI已开始录音");
                std::cout << "EVENT_START_RECORD" << std::endl;
            }
            break;

            // 停止录音事件
            case AIUIConstant::EVENT_STOP_RECORD:
            {
                LOG_INFO("AIUI已停止录音");
                std::cout << "EVENT_STOP_RECORD" << std::endl;
            }
            break;

            // 出错事件
            case AIUIConstant::EVENT_ERROR:
            {
                std::ostringstream oss;
                LOG_ERROR("AIUI出错EVENT_ERROR: error = %d, des = %s", event.getArg1(), event.getInfo());
                std::cout << "EVENT_ERROR: error=" << event.getArg1() << ", des=" << event.getInfo() << std::endl;
            }
            break;

            // 连接到服务器
            case AIUIConstant::EVENT_CONNECTED_TO_SERVER:
            {
                std::string uid = event.getData()->getString("uid", "");
                std::cout << "EVENT_CONNECTED_TO_SERVER, uid=" << uid << std::endl;
                LOG_INFO("已连接到服务器, uid = %s", uid.c_str());
            }
            break;

            // 与服务器断开连接
            case AIUIConstant::EVENT_SERVER_DISCONNECTED:
            {
                LOG_INFO("与AIUI服务器断开");
                std::cout << "EVENT_SERVER_DISCONNECTED" << std::endl;
            }
            break;

            default:
                break;
        }
    }
    catch (std::exception &e)
    {
        std::cout << "Exception in callback: " << e.what() << std::endl;
        LOG_ERROR("Exception in callback: %s", e.what());
    }
    return;
}

void AvvtnCapture::handleAiuiIat(Json::Reader &reader, const char *buffer, int len)
{
    // 语音识别结果
    std::string resultStr = std::string(buffer, len);    // 注意：这里不能用string resultStr = buffer，因为buffer不一定以0结尾
    Json::Value resultJson;
    if (reader.parse(resultStr, resultJson, false))
    {
        Json::Value textJson = resultJson["text"];
        bool isWpgs          = false;
        if (textJson.isMember("pgs"))
        {
            isWpgs = true;
        }

        if (isWpgs)
        {
            iat_text_buffer_ = IatResultUtil::parsePgsIatText(textJson);
        }
        else
        {
            // 结果拼接起来
            iat_text_buffer_.append(IatResultUtil::parseIatResult(textJson));
        }

        // 是否是该次会话最后一个识别结果
        bool isLast = textJson["ls"].asBool();
        if (isLast)
        {
            LOG_INFO("IAT语音识别结果: %s", iat_text_buffer_.c_str());
            std::cout << "iat: " << iat_text_buffer_ << std::endl;
            iat_text_buffer_.clear();
        }
    }
}

void AvvtnCapture::handleAiuiTts(const Json::Reader &reader, const Json::Value content, const IAIUIEvent event, Json::Value bizParamJson, const char *buffer, int len)
{
    Json::Value empty;
    // 语音合成结果，返回url或者pcm音频
    Json::Value &&isUrl = content.get("url", empty);
    if (isUrl.asString() == "1")
    {
        // 云端返回的是url链接，可以用播放器播放
        std::cout << "tts_url=" << std::string(buffer, len) << std::endl;
        LOG_DEBUG("云端返回的是url链接 tts_url = %s", std::string(buffer, len).c_str());
    }
    else
    {
        // 云端返回的是pcm音频，分成一块块流式返回
        LOG_DEBUG("云端返回的是pcm音频, 分成一块块流式返回");
        int progress    = 0;
        int dts         = content["dts"].asInt();
        std::string tag = event.getData()->getString("tag", "");
        if (tag.find("stream_nlp_tts") == 0)
        {
            LOG_INFO("流式语义应答的合成");
            // 流式语义应答的合成
            aiui_wrapper_.listener_->tts_helper_ptr_->onOriginTtsData(tag, bizParamJson, buffer, len);
        }
        else
        {
            //LOG_INFO("FEI流式语义应答的合成 dts = %d, tts_len_ = %d, progress = %d", dts, tts_len_, progress);
            // 只有碰到开始块和(特殊情况:合成字符比较少时只有一包tts，dts = 2)，开启播放器
            if (dts == AIUIConstant::DTS_BLOCK_FIRST || dts == AIUIConstant::DTS_ONE_BLOCK || (dts == AIUIConstant::DTS_BLOCK_LAST && 0 == tts_len_))
            {

                if (aiui_pcm_player_get_state() != PCM_PLAYER_STATE_STARTED)
                {
                    LOG_DEBUG("开启PCM播放器");
                    int ret = aiui_pcm_player_start();
                }
            }

            tts_len_ += len;
            aiui_pcm_player_write(0, buffer, len, dts, progress);
        }
        // 若要保存合成音频，请打开以下开关
#if 0
        LOG_DEBUG("保存TTS音频到本地./tts.pcm");
        static FILE *tts_file = nullptr;
        if (tts_file == nullptr)
        {
            tts_file = fopen("./tts.pcm", "wb");
        }
        if (len > 0)
        {
            fwrite(buffer, 1, len, tts_file);
        }

#endif
    }
}

void AvvtnCapture::handleAiuiStreamNlp(Json::Reader &reader, const char *buffer, int len)
{
    // 语义理解结果
    std::string resultStr = std::string(buffer, len);

    Json::Value resultJson;
    if (reader.parse(resultStr, resultJson, false))
    {

        if (resultJson.isMember("nlp"))
        {
            // AIUI v2的语义结果
            Json::Value nlpJson = resultJson["nlp"];
            std::string text    = nlpJson["text"].asString();

            // 大模型语义结果
            // 流式nlp结果里面有seq和status字段
            int seq    = nlpJson["seq"].asInt();
            int status = nlpJson["status"].asInt();

            if (status == 0)
            {
                stream_nlp_index_ = 0;
                // 开始新的流式响应
                stream_nlp_answer_buffer_.clear();
                stream_nlp_answer_buffer_ = text;
            }
            else if (status == 1) {
                stream_nlp_answer_buffer_ += text;
            } 
            if (status == 2 && seq == 0)
            {
                stream_nlp_index_ = 0;
            }
            /* 多意图取最后一次问题的结果进行tts合成 */
            if (intent_cnt_ > 1)
            {
                int currentIntentIndex = 0;
                Json::Value metaNlpJson;
                Json::Value textJson = resultJson["cbm_meta"].get("text", metaNlpJson);
                if (reader.parse(textJson.asString(), metaNlpJson, false))
                {
                    currentIntentIndex = metaNlpJson["nlp"]["intent"].asInt();
                    if ((intent_cnt_ - 1) != currentIntentIndex)
                    {
                        LOG_INFO("ignore nlp: %s", resultStr.c_str());
                        std::cout << "ignore nlp:" << resultStr << std::endl;
                        return;
                    }
                }
                else
                {
                    LOG_INFO("ignore nlp: %s", resultStr.c_str());
                    std::cout << "ignore nlp:" << resultStr << std::endl;
                    return;
                }
            }

#ifndef USE_POST_SEMANTIC_TTS
            LOG_INFO("未使用应用的语义合成");
            // 如果使用应用的语义后合成不需要在调用下面的函数否则tts的播报会重复
            aiui_wrapper_.listener_->tts_helper_ptr_->addText(text, stream_nlp_index_++, status);
#endif

            LOG_INFO("大模型返回nlp语义结果: seq = %d, status = %d, answer（应答语）: %s", seq, status, text.c_str());
            std::cout << "seq=" << seq << ", status=" << status << ", answer（应答语）: " << text << std::endl;

            if (status == 2)
            {
                stream_nlp_answer_buffer_ += text;
                LOG_INFO("大模型nlp流式返回fullText = %s", stream_nlp_answer_buffer_.c_str());
                stream_nlp_answer_buffer_.clear();
            }
        }
        else
        {
            LOG_INFO("无效nlp结果");
            LOG_INFO("----------------------------------");
            LOG_INFO("nlp: %s", resultStr.c_str());
            // 无效结果，把原始结果打印出来
            std::cout << "----------------------------------" << std::endl;
            std::cout << "nlp: " << resultStr << std::endl;
        }
    }
}

void AvvtnCapture::handleCbmTidy(const std::string& resultStr)
{
    // 语义规整
    LOG_INFO("接收到AIUI返回的【语义规整cbm_tidy】");
    
    try {
        // 解析外层JSON
        auto root = nlohmann::json::parse(resultStr);
        
        // 检查cbm_tidy字段是否存在
        if (root.contains("cbm_tidy") && root["cbm_tidy"].is_object()) {
            auto& cbm_tidy = root["cbm_tidy"];
            
            // 检查text字段是否存在且为字符串
            if (cbm_tidy.contains("text") && cbm_tidy["text"].is_string()) {
                std::string text_str = cbm_tidy["text"].get<std::string>();
                
                try {
                    // 解析text字段中的JSON
                    auto text_root = nlohmann::json::parse(text_str);
                    
                    // 检查intent字段是否存在且为数组
                    if (text_root.contains("intent") && text_root["intent"].is_array()) {
                        auto& intentArray = text_root["intent"];
                        
                        // 遍历intent数组
                        for (const auto& intent : intentArray) {
                            // 使用value()方法提供默认值，避免键不存在时抛出异常
                            int index = intent.value("index", 0);
                            std::string value = intent.value("value", "");
                            
                            LOG_INFO("语义规整结果%d: %s", index, value.c_str());
                        }
                    } else {
                        LOG_WARN("text字段中缺少intent数组或intent不是数组类型");
                    }
                } catch (const nlohmann::json::parse_error& e) {
                    LOG_ERROR("解析text字段失败: %s, text_str: %s", e.what(), text_str.c_str());
                } catch (const nlohmann::json::type_error& e) {
                    LOG_ERROR("类型错误: %s", e.what());
                }
            } else {
                LOG_WARN("cbm_tidy中缺少text字段或text不是字符串类型");
            }
        } else {
            LOG_WARN("JSON中缺少cbm_tidy字段或cbm_tidy不是对象类型");
        }
    } catch (const nlohmann::json::parse_error& e) {
        LOG_ERROR("解析JSON字符串失败: %s, resultStr: %s", e.what(), resultStr.c_str());
    } catch (const std::exception& e) {
        LOG_ERROR("处理cbm_tidy时发生异常: %s", e.what());
    }
}

bool AvvtnCapture::handleCbmSemantic(const std::string& resultStr)
{
    LOG_INFO("JSON原始数据: %s", resultStr.c_str());

    LOG_INFO("接收到AIUI返回的【传统语义技能cbm_semantic】");

    try {
        // 解析外层JSON
        auto root = nlohmann::json::parse(resultStr);
        
        // 检查cbm_semantic字段是否存在
        if (!root.contains("cbm_semantic") || !root["cbm_semantic"].is_object()) {
            LOG_WARN("JSON中缺少cbm_semantic字段或cbm_semantic不是对象类型");
            return false;
        }
        
        auto& cbm_semantic = root["cbm_semantic"];
        
        // 检查text字段是否存在且为字符串
        if (!cbm_semantic.contains("text") || !cbm_semantic["text"].is_string()) {
            LOG_WARN("cbm_semantic中缺少text字段或text不是字符串类型");
            return false;
        }
        
        std::string text_str = cbm_semantic["text"].get<std::string>();
        
        try {
            // 解析text字段中的JSON
            auto text_root = nlohmann::json::parse(text_str);
            
            // 检查rc字段是否存在
            if (text_root.contains("rc") && text_root["rc"].is_number_integer()) {
                int rc = text_root["rc"].get<int>();
                
                if (rc != 0) {
                    LOG_INFO("技能结果：未命中技能");
                    return false;
                } else {
                    LOG_INFO("技能结果：命中技能");
                    is_skill = true;
                    
                    // 打印其他字段信息
                    if (text_root.contains("text") && text_root["text"].is_string()) {
                        std::string text = text_root["text"].get<std::string>();
                        LOG_INFO("技能返回内容: %s", text.c_str());
                    }
                    
                    if (text_root.contains("version") && text_root["version"].is_string()) {
                        std::string version = text_root["version"].get<std::string>();
                        LOG_INFO("技能版本: %s", version.c_str());
                    }
                    
                    if (text_root.contains("service") && text_root["service"].is_string()) {
                        std::string service = text_root["service"].get<std::string>();
                        LOG_INFO("技能名称: %s", service.c_str());
                    }

                    /*技能处理*/
                    handleSkill(text_str);
                    
                    return true;
                }
            } else {
                LOG_WARN("text字段中缺少rc字段或rc不是整数类型");
                return false;
            }
            
        } catch (const nlohmann::json::parse_error& e) {
            LOG_ERROR("解析text字段失败: %s, text_str: %s", e.what(), text_str.c_str());
            return false;
        } catch (const nlohmann::json::type_error& e) {
            LOG_ERROR("类型错误: %s", e.what());
            return false;
        }
        
    } catch (const nlohmann::json::parse_error& e) {
        LOG_ERROR("解析JSON字符串失败: %s, resultStr: %s", e.what(), resultStr.c_str());
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("处理cbm_semantic时发生异常: %s", e.what());
        return false;
    }
}

void AvvtnCapture::handleCbmToolPk(const std::string& resultStr)
{
    LOG_INFO("接收到AIUI返回的【意图落域cbm_tool_pk】");
    
    try {
        // 解析外层JSON
        auto root = nlohmann::json::parse(resultStr);
        
        // 检查cbm_tool_pk字段是否存在
        if (!root.contains("cbm_tool_pk") || !root["cbm_tool_pk"].is_object()) {
            LOG_WARN("JSON中缺少cbm_tool_pk字段或cbm_tool_pk不是对象类型");
            return;
        }
        
        auto& cbm_tool_pk = root["cbm_tool_pk"];
        
        // 检查text字段是否存在且为字符串
        if (!cbm_tool_pk.contains("text") || !cbm_tool_pk["text"].is_string()) {
            LOG_WARN("cbm_tool_pk中缺少text字段或text不是字符串类型");
            return;
        }
        
        std::string text_str = cbm_tool_pk["text"].get<std::string>();
        
        try {
            // 解析text字段中的JSON
            auto text_root = nlohmann::json::parse(text_str);
            
            // 检查pk_type字段
            if (text_root.contains("pk_type") && text_root["pk_type"].is_string()) {
                std::string pk_type = text_root["pk_type"].get<std::string>();
                LOG_INFO("落域结果判定来源模块: %s", pk_type.c_str());
            }
            
            // 检查pk_source字段
            if (text_root.contains("pk_source") && text_root["pk_source"].is_string()) {
                std::string pk_source_text_str = text_root["pk_source"].get<std::string>();
                
                try {
                    // 解析pk_source字段中的JSON
                    auto pk_source_root = nlohmann::json::parse(pk_source_text_str);
                    
                    // 检查domain字段
                    if (pk_source_root.contains("domain") && pk_source_root["domain"].is_string()) {
                        std::string domain = pk_source_root["domain"].get<std::string>();
                        LOG_INFO("落域结果: %s", domain.c_str());
                    } else {
                        LOG_WARN("pk_source字段中缺少domain字段或domain不是字符串类型");
                    }
                } catch (const nlohmann::json::parse_error& e) {
                    LOG_ERROR("解析pk_source字段失败: %s", e.what());
                } catch (const nlohmann::json::type_error& e) {
                    LOG_ERROR("pk_source字段类型错误: %s", e.what());
                }
            } else {
                LOG_WARN("text字段中缺少pk_source字段或pk_source不是字符串类型");
            }
            
        } catch (const nlohmann::json::parse_error& e) {
            LOG_ERROR("解析text字段失败: %s", e.what());
        } catch (const nlohmann::json::type_error& e) {
            LOG_ERROR("text字段类型错误: %s", e.what());
        }
        
    } catch (const nlohmann::json::parse_error& e) {
        LOG_ERROR("解析JSON字符串失败: %s", e.what());
    } catch (const std::exception& e) {
        LOG_ERROR("处理cbm_tool_pk时发生异常: %s", e.what());
    }
}

void AvvtnCapture::handleCbmRetrievalClassify(const std::string& resultStr)
{
    //LOG_INFO("JSON原始数据: %s", resultStr.c_str());

    LOG_INFO("接收到AIUI返回的【知识分类cbm_retrieval_classify】");
    
    try {
        // 解析外层JSON
        auto root = nlohmann::json::parse(resultStr);
        
        // 检查cbm_retrieval_classify字段是否存在
        if (!root.contains("cbm_retrieval_classify") || !root["cbm_retrieval_classify"].is_object()) {
            LOG_WARN("JSON中缺少cbm_retrieval_classify字段或cbm_retrieval_classify不是对象类型");
            return;
        }
        
        auto& cbm_retrieval_classify = root["cbm_retrieval_classify"];
        
        // 检查text字段是否存在且为字符串
        if (!cbm_retrieval_classify.contains("text") || !cbm_retrieval_classify["text"].is_string()) {
            LOG_WARN("cbm_retrieval_classify中缺少text字段或text不是字符串类型");
            return;
        }
        
        std::string text_str = cbm_retrieval_classify["text"].get<std::string>();
        
        try {
            // 解析text字段中的JSON
            auto text_root = nlohmann::json::parse(text_str);
            
            // 检查type字段是否存在且为整数
            if (text_root.contains("type") && text_root["type"].is_number_integer()) {
                int type = text_root["type"].get<int>();
                
                if (type == 0) {
                    LOG_INFO("不走知识查询或联网搜索");
                } else {
                    LOG_INFO("走知识查询或联网搜索");
                }
            } else {
                LOG_WARN("text字段中缺少type字段或type不是整数类型");
            }
            
        } catch (const nlohmann::json::parse_error& e) {
            LOG_ERROR("解析text字段失败: %s, text_str: %s", e.what(), text_str.c_str());
        } catch (const nlohmann::json::type_error& e) {
            LOG_ERROR("类型错误: %s", e.what());
        }
        
    } catch (const nlohmann::json::parse_error& e) {
        LOG_ERROR("解析JSON字符串失败: %s", e.what());
    } catch (const std::exception& e) {
        LOG_ERROR("处理cbm_retrieval_classify时发生异常: %s", e.what());
    }
}

void AvvtnCapture::handleCbmKnowledge(const std::string& resultStr)
{
    LOG_INFO("JSON原始数据: %s", resultStr.c_str());
    LOG_INFO("接收到AIUI返回的【知识溯源cbm_knowledge】");
    
    try {
        // 解析外层JSON
        auto root = nlohmann::json::parse(resultStr);
        
        // 检查cbm_knowledge字段是否存在
        if (!root.contains("cbm_knowledge") || !root["cbm_knowledge"].is_object()) {
            LOG_WARN("JSON中缺少cbm_knowledge字段或cbm_knowledge不是对象类型");
            LOG_INFO("未命中知识库");
            return;
        }

        auto& cbm_knowledge = root["cbm_knowledge"];
        
        // 检查text字段是否存在且为字符串
        if (!cbm_knowledge.contains("text") || !cbm_knowledge["text"].is_string()) {
            LOG_WARN("cbm_knowledge中缺少text字段或text不是字符串类型");
            LOG_INFO("未命中知识库");
            return;
        }
        
        std::string text_str = cbm_knowledge["text"].get<std::string>();
        
        try {
            // 解析text字段中的JSON数组
            auto text_array = nlohmann::json::parse(text_str);
            
            // 检查是否是数组
            if (!text_array.is_array()) {
                LOG_WARN("text字段不是有效的JSON数组");
                LOG_INFO("未命中知识库");
                return;
            }
            
            if (text_array.empty()) {
                LOG_INFO("未命中知识库");
                return;
            }
            
            // 遍历知识条目
            for (const auto& item : text_array) {
                if (!item.is_object()) {
                    continue; // 跳过非对象元素
                }
                
                LOG_INFO("知识条目开始 ----------");
                
                // 输出score
                if (item.contains("score") && item["score"].is_number_float()) {
                    double score = item["score"].get<double>();
                    LOG_INFO("score: %lf", score);
                } else if (item.contains("score") && item["score"].is_number_integer()) {
                    double score = item["score"].get<int>();
                    LOG_INFO("score: %lf", score);
                }
                
                // 输出repoId
                if (item.contains("repoId") && item["repoId"].is_string()) {
                    std::string repoId = item["repoId"].get<std::string>();
                    LOG_INFO("知识库Id: %s", repoId.c_str());
                }
                
                // 输出docName
                if (item.contains("docName") && item["docName"].is_string()) {
                    std::string docName = item["docName"].get<std::string>();
                    LOG_INFO("来源文档: %s", docName.c_str());
                }
                
                // 输出repoName
                if (item.contains("repoName") && item["repoName"].is_string()) {
                    std::string repoName = item["repoName"].get<std::string>();
                    LOG_INFO("repo名字: %s", repoName.c_str());
                }
                
                // 输出content
                if (item.contains("content") && item["content"].is_string()) {
                    std::string content = item["content"].get<std::string>();
                    LOG_INFO("内容: %s", content.c_str());
                }
                
                // 输出其他可能存在的字段
                std::vector<std::string> other_fields = {"title", "url", "author", "time"};
                for (const auto& field : other_fields) {
                    if (item.contains(field) && item[field].is_string()) {
                        LOG_INFO("%s: %s", field.c_str(), item[field].get<std::string>().c_str());
                    }
                }
                
                LOG_INFO("知识条目结束 ----------");
            }
            
            LOG_INFO("总共找到 %zu 个知识条目", text_array.size());
            if (text_array.size() > 0)
            {
                is_knowledge = true;
            }
            
        } catch (const nlohmann::json::parse_error& e) {
            LOG_ERROR("解析text字段失败: %s, text_str: %s", e.what(), text_str.c_str());
            LOG_INFO("未命中知识库");
        } catch (const nlohmann::json::type_error& e) {
            LOG_ERROR("类型错误: %s", e.what());
            LOG_INFO("未命中知识库");
        }
        
    } catch (const nlohmann::json::parse_error& e) {
        LOG_ERROR("解析JSON字符串失败: %s", e.what());
        LOG_INFO("未命中知识库");
    } catch (const std::exception& e) {
        LOG_ERROR("处理cbm_knowledge时发生异常: %s", e.what());
        LOG_INFO("未命中知识库");
    }
}
