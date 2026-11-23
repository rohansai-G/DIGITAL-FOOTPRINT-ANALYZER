#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <sqlite3.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <regex>
#include <algorithm>
#include <iomanip>
#include <cstdlib>
#include <cctype>
#include <stdexcept>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Winhttp.lib")
struct Result {
    std::string title, url, description;
};
std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return s;
}
std::string jsonEscape(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if (c >= 32) o += (char)c;
    }
    return o;
}
std::wstring wide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 1) return L"";
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], n);
    out.pop_back();
    return out;
}
std::string encode(const std::string& s) {
    std::ostringstream o;
    o << std::uppercase << std::hex;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c=='-' || c=='_' || c=='.' || c=='~') o << c;
        else o << '%' << std::setw(2) << std::setfill('0') << (int)c;
    }
    return o.str();
}
std::string jsonField(const std::string& obj, const std::string& name) {
    std::regex re("\"" + name + R"JSON("\s*:\s*"((?:\\.|[^"\\])*)")JSON");
    std::smatch m;
    if (!std::regex_search(obj, m, re)) return "";
    std::string x=m[1].str(), o;
    for(size_t i=0;i<x.size();i++) {
        if(x[i]=='\\'&&i+1<x.size()) {
            char c=x[++i];
            if(c=='n'||c=='r'||c=='t')o+=' ';
            else o+=c;
        }
        else o+=x[i];
    }
    return o;
}
std::string htmlDecode(std::string s) {
    const std::pair<std::string,std::string> entities[] = {
        {
            "&amp;","&"
        }, {
            "&quot;","\""
        }, {
            "&#x27;","'"
        }, {
            "&#39;","'"
        }, {
            "&lt;","<"
        }, {
            "&gt;",">"
        }
    };
    for (const auto& e : entities) {
        size_t pos=0;
        while((pos=s.find(e.first,pos))!=std::string::npos) {
            s.replace(pos,e.first.size(),e.second);
            pos+=e.second.size();
        }
    }
    return s;
}
std::string stripTags(std::string s) {
    s=std::regex_replace(s,std::regex("<[^>]*>"),"");
    return htmlDecode(s);
}
std::string httpGet(const std::wstring& host, const std::string& path, const std::string& source) {
    HINTERNET session=WinHttpOpen(L"DigitalFootprintAnalyzer/1.0",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!session) throw std::runtime_error("Could not initialize internet connection for "+source+".");
    WinHttpSetTimeouts(session,5000,5000,10000,10000);
    HINTERNET connection=WinHttpConnect(session,host.c_str(),INTERNET_DEFAULT_HTTPS_PORT,0);
    if(!connection) {
        WinHttpCloseHandle(session);
        throw std::runtime_error("Could not connect to "+source+".");
    }
    std::wstring wpath=wide(path);
    HINTERNET request=WinHttpOpenRequest(connection,L"GET",wpath.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
    if(!request) {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        throw std::runtime_error("Could not create "+source+" request.");
    }
    std::wstring headers=L"Accept: application/json\r\nUser-Agent: DigitalFootprintAnalyzer/1.0 (educational project)\r\n";
    BOOL ok=WinHttpSendRequest(request,headers.c_str(),(DWORD)-1L,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(request,nullptr);
    DWORD status=0,size=sizeof(status);
    if(ok) WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX);
    std::string body;
    DWORD available=0;
    if(ok&&status>=200&&status<300) {
        while(WinHttpQueryDataAvailable(request,&available)&&available>0) {
            std::vector<char> buffer(available);
            DWORD got=0;
            if(!WinHttpReadData(request,buffer.data(),available,&got))break;
            body.append(buffer.data(),got);
        }
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    if(!ok||status<200||status>=300) throw std::runtime_error(source+" request failed (HTTP "+std::to_string(status)+").");
    return body;
}
std::vector<Result> parseWikipedia(const std::string& json) {
    std::vector<Result> results;
    std::regex searchArray(R"JSON("search"\s*:\s*\[([\s\S]*?)\])JSON",std::regex::icase);
    std::smatch arrayMatch;
    if(!std::regex_search(json,arrayMatch,searchArray)) return results;
    std::string array=arrayMatch[1].str();
    std::regex objects(R"JSON(\{[^{}]*\})JSON");
    for(auto it=std::sregex_iterator(array.begin(),array.end(),objects);it!=std::sregex_iterator()&&results.size()<8;++it) {
        std::string object=it->str(), title=jsonField(object,"title"), description=stripTags(jsonField(object,"snippet"));
        if(!title.empty()) results.push_back( {
            title,"https://en.wikipedia.org/wiki/"+encode(title),description
        });
    }
    return results;
}
std::vector<Result> parseHackerNews(const std::string& json) {
    std::vector<Result> results;
    std::regex hitsArray(R"JSON("hits"\s*:\s*\[([\s\S]*?)\]\s*,\s*"nbHits")JSON",std::regex::icase);
    std::smatch arrayMatch;
    if(!std::regex_search(json,arrayMatch,hitsArray)) return results;
    std::string array=arrayMatch[1].str();
    std::regex objects(R"JSON(\{[^{}]*\})JSON");
    for(auto it=std::sregex_iterator(array.begin(),array.end(),objects);it!=std::sregex_iterator()&&results.size()<8;++it) {
        std::string object=it->str();
        std::string title=jsonField(object,"title");
        if(title.empty()) title=jsonField(object,"story_title");
        std::string url=jsonField(object,"url");
        if(url.empty()) url=jsonField(object,"story_url");
        std::string description=jsonField(object,"story_text");
        if(title.empty()||url.empty()) continue;
        results.push_back( {
            title,url,description.empty()?"Hacker News discussion or submission.":stripTags(description)
        });
    }
    return results;
}
std::vector<Result> httpSearch(const std::string& q) {
    std::vector<Result> all;
    std::string wikiPath="/w/api.php?action=query&list=search&srsearch="+encode(q)+"&format=json&utf8=1&srlimit=8&srprop=snippet";
    std::string hnPath="/api/v1/search?query="+encode(q)+"&tags=story&hitsPerPage=8";
    try {
        auto items=parseWikipedia(httpGet(L"en.wikipedia.org",wikiPath,"Wikipedia"));
        for(auto& x:items) all.push_back(x);
    } catch(const std::exception&) {
    }
    try {
        auto items=parseHackerNews(httpGet(L"hn.algolia.com",hnPath,"Hacker News"));
        for(auto& x:items) {
            bool duplicate=false;
            for(const auto& y:all) if(y.url==x.url) duplicate=true;
            if(!duplicate) all.push_back(x);
        }
    } catch(const std::exception&) {
    }
    return all;
}
std::string sha1Upper(const std::string& value) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) < 0) throw std::runtime_error("Could not initialize password hashing.");
    DWORD objectSize = 0, resultSize = 0;
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objectSize, sizeof(objectSize), &resultSize, 0) < 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        throw std::runtime_error("Could not prepare password hashing.");
    }
    std::vector<unsigned char> object(objectSize), digest(20);
    if (BCryptCreateHash(alg, &hash, object.data(), objectSize, nullptr, 0, 0) < 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        throw std::runtime_error("Could not create password hash.");
    }
    NTSTATUS st = BCryptHashData(hash, (PUCHAR)value.data(), (ULONG)value.size(), 0);
    if (st >= 0) st = BCryptFinishHash(hash, digest.data(), (ULONG)digest.size(), 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (st < 0) throw std::runtime_error("Could not hash password.");
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0');
    for (unsigned char b : digest) out << std::setw(2) << (int)b;
    return out.str();
}
std::string httpPwnedRange(const std::string& prefix) {
    HINTERNET session = WinHttpOpen(L"DigitalFootprintAnalyzer/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) throw std::runtime_error("Could not initialize the breach-check service connection.");
    WinHttpSetTimeouts(session,5000,5000,12000,12000);
    HINTERNET connection = WinHttpConnect(session, L"api.pwnedpasswords.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) {
        WinHttpCloseHandle(session);
        throw std::runtime_error("Could not connect to the password breach-check service.");
    }
    HINTERNET request = WinHttpOpenRequest(connection, L"GET", wide("/range/" + prefix).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        throw std::runtime_error("Could not create breach-check request.");
    }
    const wchar_t* headers = L"Add-Padding: true\r\nUser-Agent: DigitalFootprintAnalyzer/1.0\r\n";
    BOOL ok = WinHttpSendRequest(request, headers, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr);
    DWORD status = 0, size = sizeof(status);
    if (ok) WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    std::string body;
    if (ok && status >= 200 && status < 300) {
        DWORD available = 0;
        while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
            std::vector<char> buffer(available);
            DWORD read = 0;
            if (!WinHttpReadData(request, buffer.data(), available, &read)) break;
            body.append(buffer.data(), read);
        }
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    if (!ok) throw std::runtime_error("Password breach-check request failed. Check your internet connection.");
    if (status < 200 || status >= 300) throw std::runtime_error("Password breach-check service returned HTTP " + std::to_string(status) + ". Try again later.");
    return body;
}
std::string passwordCheckJson(const std::string& password) {
    if (password.empty()) throw std::runtime_error("Enter a password to check.");
    if (password.size() > 4096) throw std::runtime_error("Password is too long to check.");
    std::string digest = sha1Upper(password);
    std::string responseBody = httpPwnedRange(digest.substr(0, 5));
    std::string suffix = digest.substr(5);
    unsigned long long count = 0;
    std::istringstream lines(responseBody);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t colon = line.find(':');
        if (colon != std::string::npos && line.substr(0, colon) == suffix) {
            try {
                count = std::stoull(line.substr(colon + 1));
            } catch (...) {
                count = 0;
            }
            break;
        }
    }
    std::ostringstream out;
    out << "{\"found\":" << (count > 0 ? "true" : "false") << ",\"count\":" << count << ",\"message\":\"";
    out << (count > 0 ? "This password appears in known data breaches. Stop using it and change it anywhere it was reused." : "No match was found in the known Pwned Passwords corpus. This does not guarantee the password has never been exposed.");
    out << "\",\"privacy\":\"The password was hashed locally. Only the first five SHA-1 hash characters were sent; the full password was not sent to the service or saved in the database.\"}";
    return out.str();
}
struct Report {
    int id=0, score=0;
    std::string term, level;
    std::vector<Result> results;
    std::vector<std::string> indicators;
};
Report analyze(const std::string& term) {
    Report p;
    p.term=term;
    p.results=httpSearch(term);
    int social=0,sensitive=0,leak=0,publicMentions=0;
    for(const auto&r:p.results) {
        std::string t=lower(r.title+" "+r.url+" "+r.description);
        bool hasSocial=t.find("linkedin.com")!=std::string::npos||t.find("instagram.com")!=std::string::npos||t.find("facebook.com")!=std::string::npos||t.find("github.com")!=std::string::npos||t.find("reddit.com")!=std::string::npos||t.find("x.com/")!=std::string::npos||t.find("twitter.com")!=std::string::npos;
        bool hasSensitive=t.find("phone number")!=std::string::npos||t.find("home address")!=std::string::npos||t.find("date of birth")!=std::string::npos||t.find("personal email")!=std::string::npos||t.find("residential address")!=std::string::npos||t.find("personal information")!=std::string::npos||t.find("private information")!=std::string::npos||t.find("email address")!=std::string::npos||t.find("contact details")!=std::string::npos||t.find("phone")!=std::string::npos;
        bool hasLeak=t.find("pastebin")!=std::string::npos||t.find("data breach")!=std::string::npos||t.find("password leak")!=std::string::npos||t.find("data leak")!=std::string::npos||t.find("doxxing")!=std::string::npos||t.find("exposed credentials")!=std::string::npos||t.find("hacked account")!=std::string::npos||t.find("breached")!=std::string::npos;
        if(hasSocial) social++;
        if(hasSensitive) sensitive++;
        if(hasLeak) leak++;
        if(hasSocial||hasSensitive||hasLeak) publicMentions++;
    }
    if(!p.results.empty()) {
        p.score=5;
        p.indicators.push_back("Public search results found: "+std::to_string(p.results.size())+". General results alone have low weight and do not prove exposure.");
    }
    if(social) {
        p.score+=std::min(25,social*8);
        p.indicators.push_back("Search snippets contain social or developer profile links. This indicates public visibility, not confirmed personal-data exposure.");
    }
    if(sensitive) {
        p.score+=std::min(35,sensitive*12);
        p.indicators.push_back("Search snippets contain possible contact or personal-information terms. Open and verify the linked pages manually.");
    }
    if(leak) {
        p.score+=std::min(40,leak*15);
        p.indicators.push_back("Search snippets contain breach-related terms. This is only a keyword signal and does not confirm a breach.");
    }
    p.score=std::min(p.score,100);
    p.level=p.score>=70?"HIGH":p.score>=35?"MEDIUM":"LOW";
    if(p.results.empty())p.indicators.push_back("No results were returned by Wikipedia or Hacker News. This does not prove no public information exists; check the internet connection and provider availability.");
    return p;
}
void dbExec(sqlite3*d,const char*q) {
    char*e=nullptr;
    if(sqlite3_exec(d,q,nullptr,nullptr,&e)!=SQLITE_OK) {
        std::string m=e?e:"Database error";
        sqlite3_free(e);
        throw std::runtime_error(m);
    }
}
void initDb(sqlite3*d) {
    dbExec(d,"PRAGMA foreign_keys=ON;");
    dbExec(d,"CREATE TABLE IF NOT EXISTS Assessments(ID INTEGER PRIMARY KEY AUTOINCREMENT,SearchTerm TEXT NOT NULL,Score INTEGER NOT NULL CHECK(Score BETWEEN 0 AND 100),Level TEXT NOT NULL CHECK(Level IN ('LOW','MEDIUM','HIGH')),CreatedAt TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);");
    dbExec(d,"CREATE TABLE IF NOT EXISTS SearchResults(ID INTEGER PRIMARY KEY AUTOINCREMENT,AssessmentID INTEGER NOT NULL,Title TEXT,URL TEXT NOT NULL,Description TEXT,FOREIGN KEY(AssessmentID) REFERENCES Assessments(ID) ON DELETE CASCADE);");
    dbExec(d,"CREATE TABLE IF NOT EXISTS RiskIndicators(ID INTEGER PRIMARY KEY AUTOINCREMENT,AssessmentID INTEGER NOT NULL,Indicator TEXT NOT NULL,FOREIGN KEY(AssessmentID) REFERENCES Assessments(ID) ON DELETE CASCADE);");
}
int saveReport(sqlite3*d,Report&p) {
    dbExec(d,"BEGIN TRANSACTION;");
    sqlite3_stmt*s=nullptr;
    try {
        sqlite3_prepare_v2(d,"INSERT INTO Assessments(SearchTerm,Score,Level) VALUES(?,?,?)",-1,&s,nullptr);
        sqlite3_bind_text(s,1,p.term.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_int(s,2,p.score);
        sqlite3_bind_text(s,3,p.level.c_str(),-1,SQLITE_TRANSIENT);
        if(sqlite3_step(s)!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(d));
        sqlite3_finalize(s);
        s=nullptr;
        p.id=(int)sqlite3_last_insert_rowid(d);
        for(const auto&r:p.results) {
            sqlite3_prepare_v2(d,"INSERT INTO SearchResults(AssessmentID,Title,URL,Description) VALUES(?,?,?,?)",-1,&s,nullptr);
            sqlite3_bind_int(s,1,p.id);
            sqlite3_bind_text(s,2,r.title.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(s,3,r.url.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(s,4,r.description.c_str(),-1,SQLITE_TRANSIENT);
            if(sqlite3_step(s)!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(d));
            sqlite3_finalize(s);
            s=nullptr;
        }
        for(const auto&i:p.indicators) {
            sqlite3_prepare_v2(d,"INSERT INTO RiskIndicators(AssessmentID,Indicator) VALUES(?,?)",-1,&s,nullptr);
            sqlite3_bind_int(s,1,p.id);
            sqlite3_bind_text(s,2,i.c_str(),-1,SQLITE_TRANSIENT);
            if(sqlite3_step(s)!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(d));
            sqlite3_finalize(s);
            s=nullptr;
        }
        dbExec(d,"COMMIT;");
        return p.id;
    } catch(...) {
        if(s)sqlite3_finalize(s);
        dbExec(d,"ROLLBACK;");
        throw;
    }
}
std::string reportJson(const Report&p) {
    std::ostringstream o;
    o<<"{\"id\":"<<p.id<<",\"term\":\""<<jsonEscape(p.term)<<"\",\"score\":"<<p.score<<",\"level\":\""<<p.level<<"\",\"indicators\":[";
    for(size_t i=0;i<p.indicators.size();i++) {
        if(i)o<<",";
        o<<"\""<<jsonEscape(p.indicators[i])<<"\"";
    }
    o<<"],\"results\":[";
    for(size_t i=0;i<p.results.size();i++) {
        if(i)o<<",";
        const auto&r=p.results[i];
        o<<"{\"title\":\""<<jsonEscape(r.title)<<"\",\"url\":\""<<jsonEscape(r.url)<<"\",\"description\":\""<<jsonEscape(r.description)<<"\"}";
    }
    o<<"]}";
    return o.str();
}
std::string historyJson(sqlite3*d) {
    sqlite3_stmt*s=nullptr;
    sqlite3_prepare_v2(d,"SELECT a.ID,a.SearchTerm,a.Score,a.Level,a.CreatedAt,COUNT(r.ID) FROM Assessments a LEFT JOIN SearchResults r ON a.ID=r.AssessmentID GROUP BY a.ID ORDER BY a.ID DESC",-1,&s,nullptr);
    std::ostringstream o;
    o<<"[";
    bool first=true;
    while(sqlite3_step(s)==SQLITE_ROW) {
        if(!first)o<<",";
        first=false;
        o<<"{\"id\":"<<sqlite3_column_int(s,0)<<",\"term\":\""<<jsonEscape((const char*)sqlite3_column_text(s,1))<<"\",\"score\":"<<sqlite3_column_int(s,2)<<",\"level\":\""<<sqlite3_column_text(s,3)<<"\",\"date\":\""<<sqlite3_column_text(s,4)<<"\",\"count\":"<<sqlite3_column_int(s,5)<<"}";
    }
    sqlite3_finalize(s);
    o<<"]";
    return o.str();
}
class RiskAnalyzer {
    public:
    virtual ~RiskAnalyzer() = default;
    virtual Report analyze(const std::string& searchTerm) = 0;
};
class FootprintRiskAnalyzer : public RiskAnalyzer {
    public:
    Report analyze(const std::string& searchTerm) override {
        return ::analyze(searchTerm);
    }
};
class PasswordRiskChecker {
    public:
    std::string check(const std::string& password) const {
        return passwordCheckJson(password);
    }
};
class AssessmentRepository {
    private:
    sqlite3* database;
    public:
    explicit AssessmentRepository(sqlite3* db) : database(db) {
    }
    void initialize() {
        initDb(database);
    }
    int save(Report& report) {
        return saveReport(database, report);
    }
    std::string getHistory() const {
        return historyJson(database);
    }
};
std::string bodyField(const std::string&body,const std::string&key) {
    return jsonField(body,key);
}
std::string response(const std::string&status,const std::string&type,const std::string&body) {
    std::ostringstream o;
    o<<"HTTP/1.1 "<<status<<"\r\nContent-Type: "<<type<<"; charset=utf-8\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Headers: Content-Type\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\nContent-Length: "<<body.size()<<"\r\nConnection: close\r\n\r\n"<<body;
    return o.str();
}
void sendAll(SOCKET s,const std::string&data) {
    const char*p=data.c_str();
    int left=(int)data.size();
    while(left>0) {
        int n=send(s,p,left,0);
        if(n<=0)break;
        p+=n;
        left-=n;
    }
}
std::string readRequest(SOCKET client) {
    std::string req;
    char buf[8192];
    int n=recv(client,buf,sizeof(buf),0);
    if(n>0)req.append(buf,n);
    size_t h=req.find("\r\n\r\n");
    if(h==std::string::npos)return req;
    size_t cl=req.find("Content-Length:");
    if(cl==std::string::npos)cl=req.find("content-length:");
    size_t wanted=0;
    if(cl!=std::string::npos) {
        size_t e=req.find("\r\n",cl);
        wanted=(size_t)std::stoul(req.substr(req.find(':',cl)+1,e-(req.find(':',cl)+1)));
    }
    size_t have=req.size()-h-4;
    while(have<wanted) {
        n=recv(client,buf,sizeof(buf),0);
        if(n<=0)break;
        req.append(buf,n);
        have=req.size()-h-4;
    }
    return req;
}
int main() {
    sqlite3* db = nullptr;
    if (sqlite3_open("footprint.db", &db) != SQLITE_OK) {
        std::cerr << "Cannot open footprint.db\n";
        return 1;
    }
    AssessmentRepository repository(db);
    FootprintRiskAnalyzer footprintAnalyzer;
    PasswordRiskChecker passwordChecker;
    try {
        repository.initialize();
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        sqlite3_close(db);
        return 1;
    }
    WSADATA w;
    if(WSAStartup(MAKEWORD(2,2),&w)!=0) {
        std::cerr<<"Winsock failed\n";
        sqlite3_close(db);
        return 1;
    }
    SOCKET server=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    sockaddr_in addr {
    };
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    addr.sin_port=htons(8080);
    if(server==INVALID_SOCKET||bind(server,(sockaddr*)&addr,sizeof(addr))==SOCKET_ERROR||listen(server,10)==SOCKET_ERROR) {
        std::cerr<<"Could not start server on http://127.0.0.1:8080\n";
        WSACleanup();
        sqlite3_close(db);
        return 1;
    }
    std::cout<<"Open http://127.0.0.1:8080 in your browser. Press Ctrl+C to stop.\n";
    while(true) {
        SOCKET client=accept(server,nullptr,nullptr);
        if(client==INVALID_SOCKET)continue;
        try {
            std::string req=readRequest(client);
            size_t lineEnd=req.find("\r\n");
            std::string line=req.substr(0,lineEnd);
            std::istringstream ls(line);
            std::string method,path;
            ls>>method>>path;
            size_t sep=req.find("\r\n\r\n");
            std::string body=sep==std::string::npos?"":req.substr(sep+4);
            if(method=="OPTIONS") {
                sendAll(client,response("204 No Content","text/plain",""));
            }
            else if(method=="GET"&&(path=="/"||path=="/index.html"||path=="/style.css"||path=="/app.js")) {
                std::string file=path=="/"?"index.html":path.substr(1);
                std::string content=readFile(file);
                std::string type=file=="index.html"?"text/html":file=="style.css"?"text/css":"application/javascript";
                if(content.empty())sendAll(client,response("404 Not Found","text/plain","File not found. Keep index.html, style.css, and app.js beside server.exe."));
                else sendAll(client,response("200 OK",type,content));
            }
            else if(method=="GET"&&path=="/api/history") {
                sendAll(client,response("200 OK","application/json",repository.getHistory()));
            }
            else if(method=="POST"&&path=="/api/password-check") {
                std::string password=bodyField(body,"password");
                try {
                    sendAll(client,response("200 OK","application/json",passwordChecker.check(password)));
                }
                catch(const std::exception&e) {
                    sendAll(client,response("502 Bad Gateway","application/json","{\"error\":\""+jsonEscape(e.what())+"\"}"));
                }
            }
            else if(method=="POST"&&path=="/api/analyze") {
                std::string term=bodyField(body,"term");
                if(term.empty())sendAll(client,response("400 Bad Request","application/json","{\"error\":\"Enter a search term.\"}"));
                else try {
                    Report p=footprintAnalyzer.analyze(term);
                    repository.save(p);
                    sendAll(client,response("200 OK","application/json",reportJson(p)));
                }
                catch(const std::exception&e) {
                    sendAll(client,response("502 Bad Gateway","application/json","{\"error\":\""+jsonEscape(e.what())+"\"}"));
                }
            }
            else sendAll(client,response("404 Not Found","application/json","{\"error\":\"Not found\"}"));
        } catch(...) {
            sendAll(client,response("500 Internal Server Error","application/json","{\"error\":\"Server error\"}"));
        }
        closesocket(client);
    }
    closesocket(server);
    WSACleanup();
    sqlite3_close(db);
    return 0;
}
