#include "main.h"

AS_Logs ASLogs;

IUtilsApi* utils;
IVEngineServer2* engine = nullptr;
CGlobalVars* gpGlobals = nullptr;
IAdminApi* admin_api;

PLUGIN_EXPOSE(AS_Logs, ASLogs);
SH_DECL_HOOK0_void(IServerGameDLL, GameServerSteamAPIActivated, SH_NOATTRIB, 0);

CSteamGameServerAPIContext g_steamAPI;
ISteamHTTP* g_http = nullptr;

CGameEntitySystem* g_pGameEntitySystem = nullptr;
CEntitySystem* g_pEntitySystem = nullptr;

string webhook;
bool bDebug = false;
static bool pluginLoaded = false;

bool consoleDontSend =  false;

void dbgmsg(const char* fmt, ...)
{
    if (!bDebug) return;

    char buf[1024];
    va_list va;
    va_start(va, fmt);
    V_vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);

    ConColorMsg(Color(0, 200, 255, 255), "[%s] %s\n", g_PLAPI->GetLogTag(), buf); // Чат гпт сказал что эта функция устарела, не знаю правда ли
}




void LoadConfig()
{
    KeyValues* config = new KeyValues("Config");
    const char* path = "addons/configs/AS_Logs/config.ini";
    if (!config->LoadFromFile(g_pFullFileSystem, path)) {
        delete config;
        return;
    }

    webhook = config->GetString("webhook");
    bDebug = config->GetBool("debug_on");
    consoleDontSend = config->GetBool("consoleDontSend");
    delete config;
}


class CWebhookCallback
{
public:
    CWebhookCallback(HTTPRequestHandle hRequest) : m_hRequest(hRequest)
    {
        m_pCallResult = new CCallResult<CWebhookCallback, HTTPRequestCompleted_t>();
    }

    ~CWebhookCallback()
    {
        delete m_pCallResult;
    }

    void Send(SteamAPICall_t hCall)
    {
        m_pCallResult->Set(hCall, this, &CWebhookCallback::OnHTTPRequestCompleted);
    }

    void OnHTTPRequestCompleted(HTTPRequestCompleted_t* pCallback, bool bIOFailure)
    {
        if (bIOFailure)
            dbgmsg("Webhook request failed: I/O failure");
        else if (pCallback->m_eStatusCode != 200 && pCallback->m_eStatusCode != 204)
            dbgmsg("Webhook request failed with status code: %d", pCallback->m_eStatusCode);
        else
            dbgmsg("Webhook sent successfully");

        if (g_http)
            g_http->ReleaseHTTPRequest(m_hRequest);

        delete this;
    }

private:
    HTTPRequestHandle m_hRequest;
    CCallResult<CWebhookCallback, HTTPRequestCompleted_t>* m_pCallResult;
};

void SendWebhookAsync(const string& webhookUrl, const string& jsonData)
{
    if (webhookUrl.empty()) {
        dbgmsg("Webhook URL is empty, skipping send.");
        return;
    }

    if (!g_http) {
        dbgmsg("SteamHTTP not available");
        return;
    }

    HTTPRequestHandle hRequest = g_http->CreateHTTPRequest(k_EHTTPMethodPOST, webhookUrl.c_str());
    if (hRequest == INVALID_HTTPREQUEST_HANDLE) {
        dbgmsg("Failed to create HTTP request");
        return;
    }

    g_http->SetHTTPRequestHeaderValue(hRequest, "Content-Type", "application/json");
    g_http->SetHTTPRequestRawPostBody(hRequest, "application/json", (uint8*)jsonData.c_str(), jsonData.size());

    SteamAPICall_t hCall;
    if (g_http->SendHTTPRequest(hRequest, &hCall)) {
        CWebhookCallback* pCallback = new CWebhookCallback(hRequest);
        pCallback->Send(hCall);
        dbgmsg("Webhook request sent successfully");
    }
    else {
        dbgmsg("Failed to send HTTP request");
        g_http->ReleaseHTTPRequest(hRequest);
    }
}

string CleanASCII(const char* s) {
    std::string r;
    for (size_t i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x20 && c <= 0x7E)  // только валидные ASCII
            r.push_back(c);
    }
    return r;
}


void SendWebhookToDiscord(int adminSlot, int targetSlot, const char* reason, int PunishType, int duration, const char* pSteamID, const char* pName, int method)
{
    string description;
    // method 1 = OnPlayerPunish+ method 2 OnPlayerUnpunish method 3 OnOfflinePlayerpunish method 4 OnOfflinePlayerUnP
    CCSPlayerController *admin = CCSPlayerController::FromSlot(adminSlot);
    string adminName;
    string adminSteamID;
    if (!admin || adminSlot == -1) {
        adminName = "Console";
        adminSteamID = "Отсутствует";
        if (consoleDontSend) {
            return;
        }
    } else {
        adminName = admin->GetPlayerName();
        adminSteamID = to_string(admin->m_steamID);
    }

    CCSPlayerController *target = CCSPlayerController::FromSlot(targetSlot);
    string targetName;
    string targetSteamID;
    if (!target) {
        targetName = "Неизвестно";
        targetSteamID = "Неизвестно";
    } else {
        targetName = target->GetPlayerName();
        targetSteamID = to_string(target->m_steamID);
    }

    string PunishTypeName;
    switch (PunishType) {
        case 0:
            PunishTypeName = "Бан";
            break;
        case 1:
            PunishTypeName = "Мут микрофона";
            break;
        case 2:
            PunishTypeName = "Мут текстовог чат";
            break;
        case 3:
            PunishTypeName = "Полный мут";
            break;
        default:
            PunishTypeName = string("Вид не определён") + "(" +to_string(PunishType)+")";
    }
    switch (method) {
        case 1:
            description = "🛑 Новое наказание";
            break;
        case 2:
            description = "🙏 Наказание снято";
            break;
        case 3:
            description = "🛑 Новое оффлайн наказание";
            break;
        case 4:
            description = "🙏 Наказание снято оффлайн";
            break;
        default:
            description = "Ошибка.";
            break;
    }
    try
    {
        json j;
        j["content"] = "";

        json embeds = json::array();
        json jemb;
        jemb["description"] = "## "+description;
        jemb["color"] = 0xFFFFFF;

        json jfields = json::array();

        json jField;
        jField["name"] = "👤 Администратор";
        jField["value"] = "[`" + adminName + "`](https://steamcommunity.com/profiles/" + adminSteamID + ")";
        jField["inline"] = true;
        jfields.push_back(jField);

        // STEAM ID Админа
        jField = json();
        jField["name"] = "⚙️ STEAM ID:";
        jField["value"] = "```" + adminSteamID + "```";
        jField["inline"] = true;
        jfields.push_back(jField);

        // Пустое после
        jField = json();
        jField["name"] = "⠀";
        jField["value"] = "";
        jField["inline"] = false;
        jfields.push_back(jField);

        // Ник нарушителя
        if (method == 1 || method == 2 ) { // тут передается targetSlot, поэтому не нужно изменять
            jField = json();
            jField["name"] = "🎯 Нарушитель";
            jField["value"] = "[`" + targetName + "`](https://steamcommunity.com/profiles/" + targetSteamID + ")";
            jField["inline"] = true;
            jfields.push_back(jField);
        } else if (method == 3){ // тут сразу передается ник и стим айди, поэтому не нужно искать их через
            jField = json();
            jField["name"] = "🎯 Нарушитель";
            jField["value"] = string("[`") + pName + "`](https://steamcommunity.com/profiles/" + pSteamID + ")";
            jField["inline"] = true;
            jfields.push_back(jField);
        }else if (method == 4){ // тут сразу передается ник и стим айди, поэтому не нужно искать их через
            jField = json();
            jField["name"] = "🎯 Нарушитель";
            jField["value"] = string("`Отсутствует из-за оффлайн`");
            jField["inline"] = true;
            jfields.push_back(jField);
        }


        // STEAM ID нарушителя
        if (method == 1 || method == 2) {
            // тут не передается чар с стимID, поэтому мы ищем его через CSSPlayerController и слот игрока
            jField = json();
            jField["name"] = "⚙️ STEAM ID:";
            jField["value"] = "```" + targetSteamID + "```";
            jField["inline"] = true;
            jfields.push_back(jField);
        } // ВОТ ЭТА ХУЕТА НЕ БЫЛА ЗАКРЫТА
        if (method == 3 || method == 4) { // Тут напрямую передается STEAMID
            jField = json();
            jField["name"] = "⚙️ STEAM ID:";
            jField["value"] = string("```") + CleanASCII(pSteamID) + "```";
            jField["inline"] = true;
            jfields.push_back(jField);
        }

        // Пустое после
        jField = json();
        jField["name"] = "⠀";
        jField["value"] = "";
        jField["inline"] = false;
        jfields.push_back(jField);

        // Причина наказания
        if (method == 1 || method == 3) { // Причина указывается только в наказаниях
            jField = json();
            jField["name"] = "📑 Причина";
            jField["value"] = string("```") + (reason ? reason : "не указана") + "```";
            jField["inline"] = false;
            jfields.push_back(jField);
        }

        // Длительность
        if (method == 1 || method == 3) { // Длительность указывается только в наказаниях
            jField = json();
            jField["name"] = "⌛ Длительность";
            jField["value"] =  string("```") + to_string(duration) + " сек." + "```";
            jField["inline"] = false;
            jfields.push_back(jField);
        }

        // Вид наказания
        jField = json();
        jField["name"] = "🔨 Вид наказания";
        jField["value"] = string("```") + PunishTypeName + "```";
        jField["inline"] = false;
        jfields.push_back(jField);

        jemb["fields"] = jfields;
        embeds.push_back(jemb);
        j["embeds"] = embeds;

        string jsonStr = j.dump();


        SendWebhookAsync(webhook, jsonStr);
    }
    catch (const exception& e)
    {
        if (utils)
            utils->ErrorLog("[%s] Exception in SendWebhookToDiscord: %s", g_PLAPI->GetLogTag(), e.what());
    }
}


CGameEntitySystem* GameEntitySystem()
{
    return utils ? utils->GetCGameEntitySystem() : nullptr;
}

void StartupServer()
{
    g_pGameEntitySystem = GameEntitySystem();
    g_pEntitySystem = utils->GetCEntitySystem();
    gpGlobals = utils->GetCGlobalVars();
    META_CONPRINTF("%s Plugin started successfully. Marking system ready.\n", g_PLAPI->GetLogTag());
}

void AS_Logs::OnGameServerSteamAPIActivated()
{
    g_steamAPI.Init();
    g_http = g_steamAPI.SteamHTTP();
    if (g_http) dbgmsg("SteamHTTP initialized successfully");
    else dbgmsg("Failed to get SteamHTTP");
    RETURN_META(MRES_IGNORED);
}

CON_COMMAND_F(mm_as_logs_reload, "Reloading AS_Logs plugin config", FCVAR_SERVER_CAN_EXECUTE) {
    LoadConfig();
    META_CONPRINTF("AS_Logs plugin reloaded.\n");
}
bool AS_Logs::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
    PLUGIN_SAVEVARS();


    GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetServerFactory, g_pSource2Server, ISource2Server, SOURCE2SERVER_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetServerFactory, g_pSource2GameClients, IServerGameClients, SOURCE2GAMECLIENTS_INTERFACE_VERSION);

    SH_ADD_HOOK(IServerGameDLL, GameServerSteamAPIActivated, g_pSource2Server, SH_MEMBER(this, &AS_Logs::OnGameServerSteamAPIActivated), false);

    ConVar_Register(FCVAR_SERVER_CAN_EXECUTE | FCVAR_GAMEDLL);
    g_SMAPI->AddListener(this, this);


    return true;
}


// Метод проверяющий все ли нужные плагины загружены
// Так-же тут идет "подписка" на наказание игроков, которое передает данные и отправляет вебхук :p
void AS_Logs::AllPluginsLoaded() {
    int ret;
    utils = (IUtilsApi*)g_SMAPI->MetaFactory(Utils_INTERFACE, &ret, nullptr);
    if (ret == META_IFACE_FAILED) {
        ConColorMsg(Color(255,0,0,255), "[%s] Missing Utils system plugin\n", g_PLAPI->GetLogTag());
        engine->ServerCommand(("meta unload " + std::to_string(g_PLID)).c_str());
        return;
    }

    admin_api = (IAdminApi*)g_SMAPI->MetaFactory(Admin_INTERFACE, &ret, nullptr);
    if (ret == META_IFACE_FAILED) {
        utils->ErrorLog("[%s] Missing Admin system plugin", g_PLAPI->GetLogTag());
        engine->ServerCommand(("meta unload " + std::to_string(g_PLID)).c_str());
        return;
    }

    utils->StartupServer(g_PLID, StartupServer);
    LoadConfig();
    pluginLoaded = true;


    admin_api->OnPlayerPunish(g_PLID,
    [](int iSlot, int iType, int iTime, const char* szReason, int iAdminID) {
        SendWebhookToDiscord(iAdminID,iSlot,szReason,iType,iTime,"none","none",1);
    });

    admin_api->OnPlayerUnpunish(g_PLID,[](int iSlot, int iType, int iAdminID){
        SendWebhookToDiscord(iAdminID,iSlot,"none",iType,0,"none","none",2);
    });

    admin_api->OnOfflinePlayerPunish(g_PLID,[](const char* szSteamID64, const char* szName, int iType, int iTime, const char* szReason, int iAdminID) {
        SendWebhookToDiscord(iAdminID,0,szReason,iType,iTime,szSteamID64,szName,3);
    });
}
// Выгрузка плагина
bool AS_Logs::Unload(char* error, size_t maxlen)
{
    SH_REMOVE_HOOK(IServerGameDLL, GameServerSteamAPIActivated, g_pSource2Server, SH_MEMBER(this, &AS_Logs::OnGameServerSteamAPIActivated), false);
    g_steamAPI.Clear();
    utils->ClearAllHooks(g_PLID);
    ConVar_Unregister();
    return true;
}


const char* AS_Logs::GetAuthor() { return "niffox"; }
const char* AS_Logs::GetDate() { return __DATE__; }
const char* AS_Logs::GetDescription() { return "Simple plugin for admin logs"; }
const char* AS_Logs::GetLicense() { return "Free"; }
const char* AS_Logs::GetLogTag() { return "[AS] Logs"; }
const char* AS_Logs::GetName() { return "[AS] Logs"; }
const char* AS_Logs::GetURL() { return ""; }
const char* AS_Logs::GetVersion() { return "1.0.2"; }
