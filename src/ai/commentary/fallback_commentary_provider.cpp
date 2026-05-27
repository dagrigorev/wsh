#include "fallback_commentary_provider.h"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <string>

namespace wsh {

FallbackCommentaryProvider::FallbackCommentaryProvider() {}
FallbackCommentaryProvider::~FallbackCommentaryProvider() {}

/* Extract the first word (command name) from a command line. */
static std::string ExtractFirstWord(const std::string& cmd) {
    size_t start = cmd.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = cmd.find_first_of(" \t\r\n", start);
    std::string word = cmd.substr(start, end - start);
    /* Lowercase for matching */
    for (auto& c : word) c = (char)tolower((unsigned char)c);
    return word;
}

/* Detect dangerous rm -r/-rf patterns. */
static bool IsDangerous(const std::string& cmd) {
    if (cmd.find("rm") == std::string::npos) return false;
    return cmd.find("-rf") != std::string::npos
        || cmd.find("-fr") != std::string::npos
        || cmd.find("-r") != std::string::npos;
}

/* Detect Russian Cyrillic in command string. */
static bool HasCyrillic(const std::string& s) {
    for (unsigned char c : s) {
        if (c >= 0xC0 && c <= 0xFF) return true;
    }
    return false;
}

struct CommentEntry {
    const char* cmd;
    const char* ru;
    const char* en;
};

/* clang-format off */
static const CommentEntry kComments[] = {
    {"ls",      "Смотрим, что тут лежит. Чисто? Или завалено?",
                "Peeking inside the directory. Organized or chaos?"},
    {"dir",     "dir — для тех, кто помнит DOS. Ностальгия!",
                "dir — for those who remember the DOS days. Respect."},
    {"cd",      "Меняем дислокацию. Штурман, курс проложен!",
                "Navigating the filesystem. Where are we going?"},
    {"rm",      "Удаление — дело необратимое. Бэкап есть?",
                "Deleting files. Hope you have a backup... or not."},
    {"rmdir",   "Папка уходит в небытие. Прощай, директория!",
                "Goodbye, directory. You had a good run."},
    {"mkdir",   "Новая директория появляется на свет. Жизнь!",
                "A new directory is born. The circle of files continues."},
    {"cp",      "Копируем! Два экземпляра лучше, чем один.",
                "Copying files. Two is better than one, right?"},
    {"mv",      "Переезд файла. Грузчики уже в пути.",
                "Moving files. The relocation committee approves."},
    {"touch",   "Нежное прикосновение — и файл существует.",
                "A gentle touch brings a new file into existence."},
    {"cat",     "Читаем файл. Кот следит одобрительно.",
                "Reading the file. The cat approves."},
    {"less",    "less — больше, чем кажется.",
                "less is more. Literally."},
    {"more",    "Побольше контента. Листаем!",
                "Scrolling through the content. Take your time."},
    {"head",    "Заглядываем в начало. Первое впечатление.",
                "Checking the top of the file. First impressions matter."},
    {"tail",    "Хвост файла. Самое свежее внизу!",
                "The tail end of the file. Latest and greatest."},
    {"grep",    "Ищем! Текст не спрячется от grep.",
                "Searching! No text can hide from grep."},
    {"find",    "Детектив включён. Файл будет найден.",
                "Detective mode ON. That file has nowhere to run."},
    {"echo",    "Голос терминала. Слышите эхо?",
                "The terminal speaks. Echo, echo, echo..."},
    {"pwd",     "Ты здесь. Именно здесь. Сейчас.",
                "You are here. Right here. At this exact moment."},
    {"clear",   "Чистый лист! Новая страница истории.",
                "Clean slate! Fresh start, fresh terminal."},
    {"cls",     "Экран чист. Мысли тоже? Вряд ли.",
                "Screen cleared. Mind too? Probably not."},
    {"exit",    "Уходишь? Терминал будет скучать.",
                "Leaving already? The terminal will miss you."},
    {"sudo",    "sudo — магическое заклинание. Осторожно с силой!",
                "sudo: the magic word. With great power comes great responsibility."},
    {"git",     "git — спаситель кода. Надеюсь, ты коммитил.",
                "git: the saviour of codebases. Did you commit your work?"},
    {"cmake",   "CMake поднимает паруса. Ждём попутного ветра сборки.",
                "CMake sets sail. May the build winds be in your favour."},
    {"make",    "make в деле! Исходники трепещут.",
                "make invoked! The source files tremble with anticipation."},
    {"dotnet",  ".NET в строю! Майкрософт бы порадовался.",
                ".NET is in the house. Microsoft approves."},
    {"npm",     "npm install... это ненадолго. Или надолго. Как повезёт.",
                "npm install... quick or eternal? Only the gods know."},
    {"yarn",    "Yarn крутит нити зависимостей. Философия!",
                "Yarn weaves the threads of dependencies. Very zen."},
    {"pip",     "pip — Python укомплектован. Вперёд, пакеты!",
                "pip: Python's shopping cart is loading."},
    {"python",  "Питон ползёт. Неторопливо, но уверенно.",
                "Python slithers forward. Slow and steady."},
    {"node",    "Node.js не спит. Событийный цикл крутится!",
                "Node.js: the event loop never sleeps."},
    {"docker",  "Docker стартует контейнер. Виртуальный мир оживает.",
                "Docker fires up a container. Welcome to the virtual world."},
    {"ssh",     "Подключаемся к далёкой машине. Телепортация по SSH.",
                "Connecting remotely. SSH teleportation engaged."},
    {"ping",    "Пингуем! Есть ли жизнь на том конце?",
                "Pinging! Is anybody out there?"},
    {"curl",    "Запрос в интернет. Что принесёт сеть?",
                "Reaching out to the internet. What will it say?"},
    {"wget",    "Скачиваем. Прогресс-бар — лучший аниматор.",
                "Downloading. Progress bars: the only good loading screen."},
    {"chmod",   "Раздаём права. Власть — это ответственность.",
                "Granting permissions. Power comes with responsibility."},
    {"chown",   "Меняем владельца. Передача имущества оформлена.",
                "Changing ownership. The handover is complete."},
    {"tar",     "Архив открывается. Что там внутри?",
                "Cracking open the archive. Treasure awaits."},
    {"zip",     "Упаковываем. Компактность — добродетель.",
                "Compressing. Efficiency is a virtue."},
    {"unzip",   "Разворачиваем содержимое. Сюрприз!",
                "Unzipping. Surprise inside!"},
    {"history", "Листаем прошлое. История не лжёт.",
                "Looking back at history. The terminal never forgets."},
    {"env",     "Смотрим окружение. Переменные на месте?",
                "Checking the environment. Are all variables accounted for?"},
    {"export",  "Экспортируем переменную. Добро пожаловать в окружение!",
                "Exporting to the environment. Welcome to the club, variable."},
    {"source",  "Загружаем скрипт в текущую оболочку. Магия!",
                "Sourcing the script. Shell magic at work."},
    {"which",   "Ищем, где живёт команда. Детектив доволен.",
                "Hunting down where that command lives. Detective work."},
    {"man",     "Читаем мануал. Правильно! RTFM.",
                "Reading the manual. The correct approach. RTFM achieved."},
    {"help",    "Помощь — это хорошо. Не стесняйся спрашивать.",
                "Asking for help. Bold and wise move."},
    {"ai",      "ИИ запрашивает ИИ. Философия рекурсии.",
                "AI consulting AI. Recursive philosophy."},
    {"wsh",     "Терминал внутри терминала. Inception!",
                "Terminal inside a terminal. We need to go deeper."},
    {"ipconfig","Проверяем сеть. Ты онлайн?",
                "Checking network config. Are you even online?"},
    {"netstat", "Сетевая разведка. Кто куда подключён?",
                "Network reconnaissance. Who's talking to whom?"},
    {"taskkill","Прерываем процесс! Всё, дружок.",
                "Terminating process. Your time is up."},
    {"cls",     "Экран очищен. Слоны улетели.",
                "Screen cleared. Fresh canvas."},
    {NULL, NULL, NULL}
};
/* clang-format on */

std::string FallbackCommentaryProvider::Generate(const std::string& command) {
    if (command.empty()) return "";

    bool ru = HasCyrillic(command);
    std::string first = ExtractFirstWord(command);

    /* Dangerous patterns override table lookup so the full command is visible */
    if (IsDangerous(command)) {
        return ru
            ? "\xd0\x9e\xd0\xa1\xd0\x9e\xd0\x91\xd0\x95\xd0\x9d\xd0\x9d\xd0\x9e! `"
              + command
              + "` \xe2\x80\x94 \xd0\xbd\xd0\xb5\xd0\xbe\xd0\xb1\xd1\x80\xd0\xb0\xd1\x82\xd0\xb8\xd0\xbc\xd0\xb0\xd1\x8f \xd0\xbe\xd0\xbf\xd0\xb5\xd1\x80\xd0\xb0\xd1\x86\xd0\xb8\xd1\x8f. \xd0\x91\xd1\x8d\xd0\xba\xd0\xb0\xd0\xbf \xd0\xb5\xd1\x81\xd1\x82\xd1\x8c?"
            : "WARNING: `" + command + "` is irreversible. Do you have a backup?";
        /* ru: "ОСОБЕННО! `<cmd>` — необратимая операция. Бэкап есть?" */
    }

    /* Look up command in table */
    for (int i = 0; kComments[i].cmd != NULL; i++) {
        if (first == kComments[i].cmd) {
            return ru ? kComments[i].ru : kComments[i].en;
        }
    }

    /* Generic fallback — include the full command so callers can display it */
    if (!first.empty()) {
        if (ru) {
            return "\xd0\x97\xd0\xb0\xd0\xbf\xd1\x83\xd1\x81\xd0\xba\xd0\xb0\xd0\xb5\xd0\xbc `"
                   + command + "`. \xd0\x9f\xd0\xbe\xd1\x81\xd0\xbc\xd0\xbe\xd1\x82\xd1\x80\xd0\xb8\xd0\xbc, \xd1\x87\xd1\x82\xd0\xbe \xd0\xb2\xd1\x8b\xd0\xb9\xd0\xb4\xd0\xb5\xd1\x82.";
            /* "Запускаем `<full command>`. Посмотрим, что выйдет." */
        } else {
            return "Running `" + command + "`. Let's see what happens.";
        }
    }

    return "";
}

} /* namespace wsh */
