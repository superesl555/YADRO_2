#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <queue>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

struct Time {
    int minutes;
    Time() : minutes(0) {}
    explicit Time(int m) : minutes(m) {}

    static bool parse(const std::string &s, Time &out) {
        static const std::regex re(R"((\d{2}):(\d{2}))");
        std::smatch m;
        if (!std::regex_match(s, m, re)) return false;
        int hh = std::stoi(m[1]);
        int mm = std::stoi(m[2]);
        if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;
        out.minutes = hh * 60 + mm;
        return true;
    }

    std::string str() const {
        int hh = minutes / 60;
        int mm = minutes % 60;
        std::ostringstream oss;
        oss << std::setw(2) << std::setfill('0') << hh << ':' << std::setw(2)
            << std::setfill('0') << mm;
        return oss.str();
    }
};

struct Event {
    Time t;
    int id{};
    std::vector<std::string> params;
    std::string rawLine;
    int lineNo{};
};

struct TableInfo {
    std::string occupant;
    Time since;
    long long busyMin = 0;
    long long revenue = 0;
};

struct ClientInfo {
    bool seated = false;
    int table = -1;
    Time seatedSince;
};

static inline long long roundUpHours(int minutes) {
    return (minutes + 59) / 60;
}

class Simulator {
  public:
    explicit Simulator(int nTables, Time open, Time close, long long price)
        : N(nTables), openTime(open), closeTime(close), pricePerHour(price),
          tables(nTables + 1)
    {}

    void addInputEvent(const Event &e) { events.emplace_back(e); }

    bool run() {
        out << openTime.str() << '\n';

        for (const auto &ev : events) {
            currentTime = ev.t;
            out << ev.rawLine << '\n';
            switch (ev.id) {
            case 1:
                handleArrival(ev);
                break;
            case 2:
                handleSit(ev);
                break;
            case 3:
                handleWait(ev);
                break;
            case 4:
                handleLeave(ev);
                break;
            default:
                break;
            }
        }
        currentTime = closeTime;
        if (!clients.empty()) {
            std::vector<std::string> names;
            names.reserve(clients.size());
            for (const auto &kv : clients) names.push_back(kv.first);
            std::sort(names.begin(), names.end());

            for (const auto &name : names) {
                auto &ci = clients[name];
                if (ci.seated) {
                    auto &tbl = tables[ci.table];
                    int dur = closeTime.minutes - ci.seatedSince.minutes;
                    tbl.busyMin += dur;
                    tbl.revenue += roundUpHours(dur) * pricePerHour;
                    tbl.occupant.clear();
                }
                out << closeTime.str() << " 11 " << name << '\n';
            }
        }

        out << closeTime.str() << '\n';

        for (int i = 1; i <= N; ++i) {
            const auto &tbl = tables[i];
            out << i << ' ' << tbl.revenue << ' ' << formatMinutes(tbl.busyMin)
                << '\n';
        }

        std::cout << out.str();
        return true;
    }

  private:
    int N;
    Time openTime, closeTime;
    long long pricePerHour;
    std::vector<TableInfo> tables;

    std::unordered_map<std::string, ClientInfo> clients;
    std::queue<std::string> waiting;

    std::vector<Event> events;

    Time currentTime;

    std::ostringstream out;

    static inline bool validName(const std::string &name) {
        static const std::regex re(R"(^[a-z0-9_-]+$)");
        return std::regex_match(name, re);
    }

    static std::string formatMinutes(long long m) {
        long long h = m / 60;
        long long mm = m % 60;
        std::ostringstream oss;
        oss << std::setw(2) << std::setfill('0') << h << ':' << std::setw(2)
            << std::setfill('0') << mm;
        return oss.str();
    }

    void error(const std::string &msg) {
        out << currentTime.str() << " 13 " << msg << '\n';
    }

    void handleArrival(const Event &e) {
        const std::string &name = e.params[0];
        if (clients.count(name) || inQueue(name)) {
            error("YouShallNotPass");
            return;
        }
        if (currentTime.minutes < openTime.minutes ||
            currentTime.minutes >= closeTime.minutes) {
            error("NotOpenYet");
            return;
        }
        clients[name] = ClientInfo{};
    }

    bool inQueue(const std::string &name) {
        std::queue<std::string> tmp = waiting;
        while (!tmp.empty()) {
            if (tmp.front() == name) return true;
            tmp.pop();
        }
        return false;
    }

    void handleSit(const Event &e) {
        const std::string &name = e.params[0];
        int tableNum = std::stoi(e.params[1]);

        auto it = clients.find(name);
        if (it == clients.end()) {
            error("ClientUnknown");
            return;
        }
        if (tableNum < 1 || tableNum > N) {
            error("PlaceIsBusy");
            return;
        }
        auto &tbl = tables[tableNum];
        if (!tbl.occupant.empty() && tbl.occupant != name) {
            error("PlaceIsBusy");
            return;
        }
        auto &ci = it->second;
        if (ci.seated && ci.table == tableNum) {
            error("PlaceIsBusy");
            return;
        }

        if (ci.seated) {
            auto &oldTbl = tables[ci.table];
            int dur = currentTime.minutes - ci.seatedSince.minutes;
            oldTbl.busyMin += dur;
            oldTbl.revenue += roundUpHours(dur) * pricePerHour;
            oldTbl.occupant.clear();
        } else {
            removeFromQueue(name);
        }

        tbl.occupant = name;
        ci.seated = true;
        ci.seatedSince = currentTime;
        ci.table = tableNum;
    }

    void removeFromQueue(const std::string &name) {
        std::queue<std::string> tmp;
        while (!waiting.empty()) {
            if (waiting.front() != name) tmp.push(waiting.front());
            waiting.pop();
        }
        waiting.swap(tmp);
    }

    void handleWait(const Event &e) {
        const std::string &name = e.params[0];

        bool freeExists = false;
        for (int i = 1; i <= N; ++i) {
            if (tables[i].occupant.empty()) {
                freeExists = true;
                break;
            }
        }
        if (freeExists) {
            error("ICanWaitNoLonger!");
            return;
        }

        if (waiting.size() >= static_cast<size_t>(N)) {
            out << currentTime.str() << " 11 " << name << '\n';
            return;
        }

        if (!clients.count(name)) {
            clients[name] = ClientInfo{};
        }
        waiting.push(name);
    }

    void handleLeave(const Event &e) {
        const std::string &name = e.params[0];
        auto it = clients.find(name);
        if (it == clients.end()) {
            error("ClientUnknown");
            return;
        }

        auto &ci = it->second;
        int freedTable = -1;
        if (ci.seated) {
            auto &tbl = tables[ci.table];
            int dur = currentTime.minutes - ci.seatedSince.minutes;
            tbl.busyMin += dur;
            tbl.revenue += roundUpHours(dur) * pricePerHour;
            tbl.occupant.clear();
            freedTable = ci.table;
        }
        clients.erase(it);
        removeFromQueue(name);

        if (freedTable != -1 && !waiting.empty()) {
            std::string next = waiting.front();
            waiting.pop();
            auto &ciNext = clients[next];
            auto &tbl = tables[freedTable];
            tbl.occupant = next;
            ciNext.seated = true;
            ciNext.table = freedTable;
            ciNext.seatedSince = currentTime;
            out << currentTime.str() << " 12 " << next << ' ' << freedTable
                << '\n';
        }
    }
};


static bool parsePositiveInt(const std::string &s, long long &value) {
    if (s.empty() || !std::all_of(s.begin(), s.end(), ::isdigit)) return false;
    try {
        value = std::stoll(s);
        return value > 0;
    } catch (...) {
        return false;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input_file>\n";
        return 1;
    }
    std::ifstream fin(argv[1]);
    if (!fin) {
        std::cerr << "Cannot open input file\n";
        return 1;
    }

    std::string line;

    int lineNo = 0;

    lineNo++;
    if (!std::getline(fin, line)) {
        std::cout << lineNo << '\n';
        return 0;
    }
    long long nTablesLL = 0;
    if (!parsePositiveInt(line, nTablesLL) || nTablesLL > 1000) {
        std::cout << lineNo << '\n';
        return 0;
    }
    int nTables = static_cast<int>(nTablesLL);

    lineNo++;
    if (!std::getline(fin, line)) {
        std::cout << lineNo << '\n';
        return 0;
    }
    std::istringstream iss2(line);
    std::string openStr, closeStr;
    if (!(iss2 >> openStr >> closeStr)) {
        std::cout << lineNo << '\n';
        return 0;
    }
    Time openTime, closeTime;
    if (!Time::parse(openStr, openTime) || !Time::parse(closeStr, closeTime) ||
        openTime.minutes >= closeTime.minutes) {
        std::cout << lineNo << '\n';
        return 0;
    }

    lineNo++;
    if (!std::getline(fin, line)) {
        std::cout << lineNo << '\n';
        return 0;
    }
    long long price = 0;
    if (!parsePositiveInt(line, price) || price > 1000000000LL) {
        std::cout << lineNo << '\n';
        return 0;
    }

    Simulator sim(nTables, openTime, closeTime, price);

    static const std::regex nameRE(R"(^[a-z0-9_-]+$)");

    Time prevEventTime(0); 

    while (std::getline(fin, line)) {
        if (line.empty()) continue;
        lineNo++;
        std::istringstream iss(line);
        std::string timeStr;
        int id;
        if (!(iss >> timeStr >> id)) {
            std::cout << lineNo << '\n';
            return 0;
        }
        Time evTime;
        if (!Time::parse(timeStr, evTime) || evTime.minutes < prevEventTime.minutes) {
            std::cout << lineNo << '\n';
            return 0;
        }
        prevEventTime = evTime;

        Event ev;
        ev.t = evTime;
        ev.id = id;
        ev.lineNo = lineNo;
        ev.rawLine = line;

        switch (id) {
        case 1: {
            std::string name;
            if (!(iss >> name) || !std::regex_match(name, nameRE) || (iss >> std::ws && !iss.eof())) {
                std::cout << lineNo << '\n';
                return 0;
            }
            ev.params.push_back(name);
            break;
        }
        case 2: {
            std::string name;
            int tableNum;
            if (!(iss >> name >> tableNum) || !std::regex_match(name, nameRE) ||
                tableNum <= 0 || tableNum > nTables || (iss >> std::ws && !iss.eof())) {
                std::cout << lineNo << '\n';
                return 0;
            }
            ev.params.push_back(name);
            ev.params.push_back(std::to_string(tableNum));
            break;
        }
        case 3: {
            std::string name;
            if (!(iss >> name) || !std::regex_match(name, nameRE) || (iss >> std::ws && !iss.eof())) {
                std::cout << lineNo << '\n';
                return 0;
            }
            ev.params.push_back(name);
            break;
        }
        case 4: {
            std::string name;
            if (!(iss >> name) || !std::regex_match(name, nameRE) || (iss >> std::ws && !iss.eof())) {
                std::cout << lineNo << '\n';
                return 0;
            }
            ev.params.push_back(name);
            break;
        }
        default:
            std::cout << lineNo << '\n';
            return 0;
        }

        sim.addInputEvent(ev);
    }

    sim.run();

    return 0;
}
