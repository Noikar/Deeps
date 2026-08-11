/**
* Copyright (c) 2011-2014 - Ashita Development Team
*
* Ashita is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* Ashita is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with Ashita.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Deeps.h"

#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

/**
 * @brief Where we look for releases, and how long we wait before looking.
 */
#define VERSION_HOST        L"api.github.com"
#define VERSION_PATH        L"/repos/Noikar/Deeps/releases/latest"
#define VERSION_RELEASE_URL "https://github.com/Noikar/Deeps/releases/latest"
#define VERSION_DELAY_MS    3000
#define VERSION_SLICE_MS    100
#define VERSION_TIMEOUT_MS  4000
#define VERSION_MAX_BYTES   65536

/**
 * @brief Tiny RAII wrapper so WinHTTP handles close on every exit path.
 */
struct httphandle_t
{
    HINTERNET handle;

    httphandle_t(HINTERNET h = NULL)
        : handle(h)
    { }
    ~httphandle_t(void)
    {
        if (this->handle != NULL)
            WinHttpCloseHandle(this->handle);
    }

    httphandle_t(const httphandle_t&) = delete;
    httphandle_t& operator=(const httphandle_t&) = delete;

    operator HINTERNET(void) const { return this->handle; }
    bool valid(void) const { return this->handle != NULL; }
};

/**
 * @brief Sleeps in small slices so an unload during the delay is not held up.
 *
 * @param state         The shared check state, watched for cancellation.
 * @param milliseconds  How long to wait in total.
 *
 * @return True if the full wait elapsed, false if the check was cancelled.
 */
static bool VersionWait(const std::shared_ptr<versioncheck_t>& state, int32_t milliseconds)
{
    for (int32_t waited = 0; waited < milliseconds; waited += VERSION_SLICE_MS)
    {
        if (state->cancelled.load())
            return false;

        std::this_thread::sleep_for(std::chrono::milliseconds(VERSION_SLICE_MS));
    }

    return !state->cancelled.load();
}

/**
 * @brief Pulls the value of the given string property out of a JSON payload.
 *
 * @note    This is a deliberate shortcut over a real JSON parser; the release
 *          endpoint hands us a flat object and we only want one field of it.
 *
 * @param json  The raw response body.
 * @param key   The property name to look up, without quotes.
 *
 * @return The property value, or an empty string if it was not found.
 */
static std::string JsonFindString(const std::string& json, const char* key)
{
    const std::string needle = std::string("\"") + key + "\"";

    auto pos = json.find(needle);
    if (pos == std::string::npos)
        return "";

    pos = json.find(':', pos + needle.length());
    if (pos == std::string::npos)
        return "";

    pos = json.find('"', pos);
    if (pos == std::string::npos)
        return "";

    const auto end = json.find('"', pos + 1);
    if (end == std::string::npos)
        return "";

    return json.substr(pos + 1, end - pos - 1);
}

/**
 * @brief Requests the latest release from GitHub.
 *
 * @param userAgent The user agent to identify ourselves with; GitHub requires one.
 * @param state     The shared check state, watched for cancellation between phases.
 *
 * @return The response body, or an empty string on any failure.
 */
static std::string VersionFetch(const std::wstring& userAgent, const std::shared_ptr<versioncheck_t>& state)
{
    httphandle_t session(WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.valid())
        return "";

    // Older systems do not negotiate TLS 1.2 by default and GitHub requires it.
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    // Bound every phase so a dead network cannot stall an unload for long.
    WinHttpSetTimeouts(session, VERSION_TIMEOUT_MS, VERSION_TIMEOUT_MS, VERSION_TIMEOUT_MS, VERSION_TIMEOUT_MS);

    httphandle_t connection(WinHttpConnect(session, VERSION_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection.valid())
        return "";

    httphandle_t request(WinHttpOpenRequest(connection, L"GET", VERSION_PATH, NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request.valid())
        return "";

    WinHttpAddRequestHeaders(request, L"Accept: application/vnd.github+json\r\n", (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        return "";

    if (!WinHttpReceiveResponse(request, NULL))
        return "";

    if (state->cancelled.load())
        return "";

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX))
        return "";

    if (status != 200)
        return "";

    std::string body;
    char buffer[2048];
    DWORD read = 0;

    while (WinHttpReadData(request, buffer, sizeof(buffer), &read) && (read > 0))
    {
        body.append(buffer, read);

        if ((body.length() >= VERSION_MAX_BYTES) || state->cancelled.load())
            break;
    }

    return body;
}

/**
 * @brief Background worker; waits, asks GitHub, then parks the result for the main thread.
 *
 * @note    This runs detached from the plugin object on purpose. It only ever touches
 *          the shared state it was handed, so an unload mid-check cannot dangle.
 */
static void VersionWorker(std::shared_ptr<versioncheck_t> state, std::wstring userAgent, int32_t delay)
{
    if (!VersionWait(state, delay))
        return;

    const auto body = VersionFetch(userAgent, state);
    if (state->cancelled.load())
        return;

    state->latest = JsonFindString(body, "tag_name");
    state->done.store(true);
}

/**
 * @brief Converts a version string into a comparable number.
 *
 * @note    The minor component is normalized to two digits so that "1.7" and
 *          "1.70" both compare equal, matching how the plugin version reads.
 *
 * @param text  A version such as "v1.07", "1.7" or "1.07.2".
 * @param out   Receives the comparable value.
 *
 * @return True if a version was parsed, false otherwise.
 */
static bool ParseVersion(const std::string& text, int32_t* out)
{
    size_t pos = 0;
    while ((pos < text.length()) && !isdigit((uint8_t)text[pos]))
        pos++;

    if (pos >= text.length())
        return false;

    int32_t major = 0;
    while ((pos < text.length()) && isdigit((uint8_t)text[pos]))
    {
        major = (major * 10) + (text[pos] - '0');
        pos++;
    }

    int32_t minor = 0;
    int32_t digits = 0;
    if ((pos < text.length()) && (text[pos] == '.'))
    {
        pos++;
        while ((pos < text.length()) && isdigit((uint8_t)text[pos]) && (digits < 2))
        {
            minor = (minor * 10) + (text[pos] - '0');
            pos++;
            digits++;
        }
    }

    while (digits < 2)
    {
        minor *= 10;
        digits++;
    }

    *out = (major * 100) + minor;
    return true;
}

/**
 * @brief Starts a version check, replacing any finished one.
 *
 * @param verbose   True for a check the user asked for; those also report success.
 */
void Deeps::StartVersionCheck(bool verbose)
{
    // The automatic check runs once per load; only a user-requested check repeats.
    if ((!verbose) && (m_VersionCheck != nullptr))
        return;

    // A check is already in flight; let it report rather than stacking another.
    if ((m_VersionCheck != nullptr) && (!m_VersionCheck->done.load()))
        return;

    if (m_VersionThread.joinable())
        m_VersionThread.join();

    m_VersionCheck = std::make_shared<versioncheck_t>();
    m_VersionCheck->verbose = verbose;

    wchar_t userAgent[64];
    swprintf_s(userAgent, _countof(userAgent), L"Deeps/%.2f (Ashita)", this->GetVersion());

    m_VersionThread = std::thread(VersionWorker, m_VersionCheck, std::wstring(userAgent), verbose ? 0 : VERSION_DELAY_MS);
}

/**
 * @brief Stops any running version check and waits for the worker to leave our code.
 *
 * @note    Joining rather than detaching matters here: the DLL can be unloaded the
 *          moment we return, which would pull the worker's code out from under it.
 */
void Deeps::StopVersionCheck(void)
{
    if (m_VersionCheck != nullptr)
        m_VersionCheck->cancelled.store(true);

    if (m_VersionThread.joinable())
        m_VersionThread.join();
}

/**
 * @brief Reports a finished version check. Called from the render thread so that
 *        all chat output happens on the thread Ashita expects it on.
 */
void Deeps::FlushVersionCheck(void)
{
    if ((m_VersionCheck == nullptr) || (m_VersionCheck->consumed) || (!m_VersionCheck->done.load()))
        return;

    m_VersionCheck->consumed = true;

    const auto verbose = m_VersionCheck->verbose;
    const auto latest  = m_VersionCheck->latest;

    // Formatted to match the "v1.07" release tags GitHub reports.
    char current[16];
    sprintf_s(current, _countof(current), "v%.2f", this->GetVersion());

    int32_t latestValue  = 0;
    int32_t currentValue = 0;
    if ((!ParseVersion(latest, &latestValue)) || (!ParseVersion(current, &currentValue)))
    {
        // Silent on failure unless the user asked for the check themselves.
        if (verbose)
        {
            m_AshitaCore->GetChatManager()->Writef(0, false, "%s%s", Ashita::Chat::Header("Deeps").c_str(),
                Ashita::Chat::Error("Unable to check for updates.").c_str());
        }
        return;
    }

    if (latestValue <= currentValue)
    {
        if (verbose)
        {
            m_AshitaCore->GetChatManager()->Writef(0, false, "%sVersion %s is up to date.",
                Ashita::Chat::Header("Deeps").c_str(), Ashita::Chat::Color2(2, current).c_str());
        }
        return;
    }

    m_AshitaCore->GetChatManager()->Writef(0, false, "%sVersion %s is available. You have %s.",
        Ashita::Chat::Header("Deeps").c_str(), Ashita::Chat::Color2(2, latest.c_str()).c_str(),
        Ashita::Chat::Color2(2, current).c_str());
    m_AshitaCore->GetChatManager()->Writef(0, false, "%s%s", Ashita::Chat::Header("Deeps").c_str(),
        Ashita::Chat::Message(VERSION_RELEASE_URL).c_str());
}
