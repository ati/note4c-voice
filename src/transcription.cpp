// SPDX-License-Identifier: GPL-3.0-only
#include "transcription.h"
#include "multipart.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <cJSON.h>

namespace {
class UploadStream : public Stream {
    MultipartBody body_;
public:
    UploadStream(const audio::Recording &r) : body_(r.pcm, r.samples) {}
    size_t size() const { return body_.size(); }
    int available() override { return body_.available(); }
    int read() override { uint8_t byte; return body_.read(&byte, 1) ? byte : -1; }
    int peek() override { return -1; }
    void flush() override {}
    size_t write(uint8_t) override { return 0; }
    size_t readBytes(char *buffer, size_t length) override { return body_.read(reinterpret_cast<uint8_t *>(buffer), length); }
};
class Response : public Stream {
public:
    static constexpr size_t limit = 16384;
    std::string text;
    bool overflow = false;
    Response() { text.reserve(limit); }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    size_t write(uint8_t byte) override { return write(&byte, 1); }
    size_t write(const uint8_t *data, size_t count) override {
        if (text.size() + count > limit) { overflow = true; return 0; }
        text.append(reinterpret_cast<const char *>(data), count); return count;
    }
};
}
std::string transcribe(const audio::Recording &recording, const char *url, const std::string &host,
                       bool *recognized) {
    if (recognized) *recognized = false;
    if (WiFi.status() != WL_CONNECTED) return voice::unavailable(host);
    WiFiClient client;
    HTTPClient request;
    request.setConnectTimeout(5000); request.setTimeout(60000);
    request.setReuse(false); request.useHTTP10(true);
    if (!request.begin(client, url)) return voice::unavailable(host);
    request.addHeader("Content-Type", String("multipart/form-data; boundary=") + MultipartBody::boundary());
    UploadStream upload(recording);
    const int status = request.sendRequest("POST", &upload, upload.size());
    Serial.printf("Transcription HTTP %d\n", status);
    std::string result;
    if (status <= 0 || status >= 500 || status == 408) result = voice::unavailable(host);
    else if (status != 200) result = "Ошибка сервиса транскрибации (HTTP " + std::to_string(status) + ").";
    else if (request.getSize() > static_cast<int>(Response::limit)) result = "Ответ сервиса транскрибации слишком большой.";
    else {
        Response response;
        const int received = request.writeToStream(&response);
        if (response.overflow) result = "Ответ сервиса транскрибации слишком большой.";
        else if (received < 0) result = voice::unavailable(host);
        else {
            cJSON *json = cJSON_ParseWithLengthOpts(response.text.c_str(), response.text.size() + 1, nullptr, true);
            const cJSON *field = json ? cJSON_GetObjectItemCaseSensitive(json, "text") : nullptr;
            if (!cJSON_IsString(field) || !field->valuestring) result = "Некорректный ответ сервиса транскрибации.";
            else {
                result = voice::limitText(field->valuestring);
                if (result.empty()) result = "Речь не распознана. Нажмите кнопку и попробуйте ещё раз.";
                else if (recognized) *recognized = true;
            }
            cJSON_Delete(json);
        }
    }
    request.end(); return result;
}
