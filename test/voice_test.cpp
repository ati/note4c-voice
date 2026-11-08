// SPDX-License-Identifier: GPL-3.0-only
#include "canvas.h"
#include "multipart.h"
#include "validation.h"
#include "voice_logic.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>

namespace {
std::string repeat(const std::string &value, unsigned count) {
    std::string result;
    while (count--) result += value;
    return result;
}
uint32_t le(const uint8_t *bytes, unsigned count) {
    uint32_t result = 0;
    for (unsigned i = 0; i < count; ++i) result |= uint32_t(bytes[i]) << (8 * i);
    return result;
}
void textCases() {
    for (const std::string value : {"я", "Ё", "🙂", "A"}) {
        const auto full = repeat(value, 160);
        assert(voice::limitText(full) == full);
        assert(voice::limitText(full + value) == repeat(value, 159) + "…");
        assert(voice::decode(full).size() == 160);
    }
    assert(voice::normalize(" \n Привет\t\tмир !\r\n") == "Привет мир !");
    assert(voice::normalize("\xef\xbb\xbfТест\xe2\x80\x8b") == "Тест");
    assert(voice::limitText("\n\t ").empty());
    for (const std::string invalid : {"\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82", "\xff", "\xe2x"}) {
        const auto decoded = voice::decode(invalid);
        assert(!decoded.empty() && decoded.front() == 0xfffd);
        for (auto cp : decoded) assert(cp <= 0x10ffff && (cp < 0xd800 || cp > 0xdfff));
        assert(voice::decode(voice::encode(decoded)) == decoded);
    }
    // All byte values and truncated prefixes must be safe, including signed char.
    std::string bytes;
    for (unsigned i = 0; i < 256; ++i) bytes += static_cast<char>(i);
    for (unsigned i = 0; i <= bytes.size(); ++i) voice::limitText(bytes.substr(0, i));
    int64_t epoch;
    assert(validation::epoch("1789832520", epoch));
    assert(!validation::epoch("9999999999", epoch));
    assert(!validation::epoch("178983252x", epoch));
    std::string host;
    assert(validation::serverUrl("http://192.168.1.10:8000/v1/audio/transcriptions", &host) && host == "192.168.1.10");
    assert(validation::serverUrl("http://whisper.local/v1/audio/transcriptions", &host) && host == "whisper.local");
    assert(validation::serverUrl("http://h/"));
    for (const char *name : {"pool.ntp.org", "a-b.c", "10.0.0.1"}) assert(validation::hostname(name));
    for (const char *name : {"-a", "a-", "a..b", ".a", "a.", "a.-b", "a-.b"}) assert(!validation::hostname(name));
    for (const char *bad : {"", "https://h/x", "http://", "http:///x", "http://h", "http://h:0/x", "http://h:65536/x",
                            "http://h:80", "http://user@h/x", "http://h/a b", "http://h/\"x", "http://-h/x"})
        assert(!validation::serverUrl(bad));
    assert(!validation::serverUrl(("http://h/" + std::string(120, 'a')).c_str()));
    for (const char *zone : {"MSK-3", "UTC0", "CET-1CEST,M3.5.0,M10.5.0/3", "EST5EDT,M3.2.0,M11.1.0", "<+0530>-5:30"})
        assert(validation::timezone(zone));
    for (const char *zone : {"", "Europe/Moscow", "MSK", "UTC+25", "CET-1CEST,M13.5.0,M10.5.0"})
        assert(!validation::timezone(zone));
}
voice::StopReason addFrames(voice::CapturePolicy &policy, float rms, unsigned frames) {
    voice::StopReason result = voice::StopReason::None;
    while (frames--) {
        assert(result == voice::StopReason::None);
        result = policy.add(rms, voice::frameSamples, false);
    }
    return result;
}
void recordingCases() {
    using voice::StopReason;
    voice::CapturePolicy silent(0);
    assert(addFrames(silent, 0, 249) == StopReason::None);
    assert(addFrames(silent, 0, 1) == StopReason::NoSpeech);
    voice::CapturePolicy sentence(80);
    assert(addFrames(sentence, 1000, 10) == StopReason::None);
    assert(addFrames(sentence, 50, 99) == StopReason::None);
    assert(addFrames(sentence, 50, 1) == StopReason::Silence);
    voice::CapturePolicy shortNoise(50);
    assert(addFrames(shortNoise, 2000, 2) == StopReason::None);
    assert(addFrames(shortNoise, 10, 248) == StopReason::NoSpeech);
    voice::CapturePolicy resumed(50);
    addFrames(resumed, 1000, 5); addFrames(resumed, 0, 80);
    addFrames(resumed, 1000, 5);
    assert(addFrames(resumed, 0, 99) == StopReason::None);
    assert(addFrames(resumed, 0, 1) == StopReason::Silence);
    voice::CapturePolicy maximum(50);
    assert(addFrames(maximum, 1000, 1499) == StopReason::None);
    assert(addFrames(maximum, 1000, 1) == StopReason::Limit);
    voice::CapturePolicy manual(50), emptyManual(50);
    addFrames(manual, 1000, 3);
    assert(manual.add(0, voice::frameSamples, true) == StopReason::Button);
    assert(emptyManual.add(0, voice::frameSamples, true) == StopReason::NoSpeech);
    voice::HighPass filter;
    int16_t last = 0;
    for (int i = 0; i < 5000; ++i) last = filter.process(12000);
    assert(std::abs(last) <= 1); // DC offset alone must not keep VAD active.
    for (int i = 0; i < 1000; ++i) filter.process(i % 2 ? 32767 : -32768);

    voice::StopButton held;
    assert(!held.update(false, 100));
    assert(!held.update(false, 1000)); // Initial wake press is never stop.
    assert(!held.update(true, 1010));
    assert(!held.update(false, 1020)); // Release bounce is not enough to arm.
    assert(!held.update(false, 1100));
    assert(!held.update(true, 1110));
    assert(!held.update(true, 1150));
    assert(!held.update(false, 1200));
    assert(!held.update(false, 1239));
    assert(held.update(false, 1240));
    assert(!held.update(false, 1300)); // One event per press.
    voice::StopButton quick;
    assert(!quick.update(true, 100)); // Wake press already released at readiness.
    assert(!quick.update(false, 200));
    assert(quick.update(false, 240));
    voice::StopButton rollover;
    assert(!rollover.update(false, 0xffffff80U));
    assert(!rollover.update(true, 0xfffffff0U));
    assert(!rollover.update(true, 24));
    assert(!rollover.update(false, 30));
    assert(rollover.update(false, 70));
}
std::vector<uint8_t> bodyWithChunks(const std::vector<int16_t> &pcm, size_t chunk) {
    MultipartBody body(pcm.data(), pcm.size());
    std::vector<uint8_t> result(body.size());
    size_t total = 0;
    assert(body.read(result.data(), 0) == 0);
    while (body.available()) {
        const size_t n = body.read(result.data() + total, chunk);
        assert(n && n <= chunk);
        total += n;
        assert(body.available() + total == body.size());
    }
    assert(total == result.size());
    assert(body.read(result.data(), chunk) == 0);
    return result;
}
void multipartCases() {
    for (size_t samples : {size_t(0), size_t(1), size_t(320), size_t(voice::maxSamples)}) {
        std::vector<int16_t> pcm(samples);
        for (size_t i = 0; i < samples; ++i) pcm[i] = static_cast<int16_t>(i);
        uint8_t header[44]; voice::wavHeader(header, samples);
        assert(std::memcmp(header, "RIFF", 4) == 0 && std::memcmp(header + 8, "WAVEfmt ", 8) == 0);
        assert(le(header + 4, 4) == samples * 2 + 36);
        assert(le(header + 20, 2) == 1 && le(header + 22, 2) == 1);
        assert(le(header + 24, 4) == 16000 && le(header + 28, 4) == 32000);
        assert(le(header + 32, 2) == 2 && le(header + 34, 2) == 16);
        assert(le(header + 40, 4) == samples * 2);
        const auto whole = bodyWithChunks(pcm, 2000000);
        for (size_t chunk : {1, 43, 257, 1460}) assert(bodyWithChunks(pcm, chunk) == whole);
    }
}
void save(const std::string &name, const uint8_t *data, size_t size) {
    std::ofstream file(name, std::ios::binary);
    file.write(reinterpret_cast<const char *>(data), size);
    assert(file.good());
}
void screenCases(const std::string &directory) {
    const std::string timestamp = "19.09.2026 · 18:42";
    const std::string sentence = "Завтра в десять утра встреча с командой. Обсудить макет приложения, проверить микрофон и отправить результаты тестирования. После встречи позвонить Андрею и уточнить детали.";
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"short", "Завтра в десять встреча с командой."},
        {"160", voice::limitText(sentence)},
        {"wide", repeat("Щ", 160)},
        {"narrow", repeat("i ", 80)},
        {"unavailable", voice::unavailable("whisper.local")},
        {"setup", "Подключитесь к Wi-Fi NOTE4C-VOICE-AC45. Пароль: NOTE4CVOICE. Откройте 192.168.4.1 и сохраните настройки."}
    };
    assert(voice::decode(cases[1].second).size() == 160);
    assert(Canvas::measure(voice::decode(timestamp), stampFont) <= 280);
    for (const auto &item : cases) {
        Canvas canvas;
        const auto stamp = item.first == "setup" ? "" : timestamp;
        assert(canvas.note(item.second, stamp));
        for (int y = 0; y < Canvas::height; ++y)
            for (int x = 0; x < Canvas::width; ++x)
                if (canvas.black(x, y)) {
                    assert(x >= 24 && x < 376);
                    assert((y >= 27 && y < 229) || (stamp[0] && y >= 248 && y < 290));
                }
        if (item.first == "setup") for (int y = 240; y < 300; ++y)
            for (int x = 0; x < 400; ++x) assert(!canvas.black(x, y));
        const auto text = voice::decode(item.second);
        bool fits = false;
        for (size_t i = 0; i < bodyFontCount; ++i) {
            const auto &font = bodyFonts[i];
            const auto lines = Canvas::wrap(text, font, 348);
            if (lines.size() * font.lineHeight > 202) continue;
            fits = true;
            std::vector<uint32_t> visible, expected;
            for (const auto &line : lines) {
                assert(Canvas::measure(line, font) <= 348);
                for (auto cp : line) if (cp != ' ') visible.push_back(cp);
            }
            for (auto cp : text) if (cp != ' ') expected.push_back(cp);
            assert(visible == expected); // No characters lost at wraps.
            std::cout << "Screen " << item.first << ": " << static_cast<unsigned>(font.size) << " px, " << lines.size() << " lines\n";
            break;
        }
        assert(fits);
        save(directory + "/screen-" + item.first + ".raw", canvas.data(), Canvas::byteCount);
    }
}
}
int main(int argc, char **argv) {
    assert(argc == 2 || argc == 4);
    textCases(); recordingCases(); multipartCases(); screenCases(argv[1]);
    if (argc == 4) {
        std::ifstream source(argv[2], std::ios::binary);
        const std::vector<char> bytes((std::istreambuf_iterator<char>(source)), {});
        assert(source && !bytes.empty() && bytes.size() % 2 == 0 && bytes.size() <= voice::maxSamples * 2);
        std::vector<int16_t> pcm(bytes.size() / 2);
        for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = static_cast<int16_t>(static_cast<uint8_t>(bytes[i * 2]) | (static_cast<uint8_t>(bytes[i * 2 + 1]) << 8));
        const auto body = bodyWithChunks(pcm, 257);
        save(argv[3], body.data(), body.size());
    }
    std::cout << "PASS: UTF-8, 160 characters, recording limits, silence, button, WAV, multipart and layout\n";
}
