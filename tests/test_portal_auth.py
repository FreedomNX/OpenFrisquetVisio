"""Exercise the real portal auth/session handlers with native HTTP/Preferences stubs.

Run: python3 tests/test_portal_auth.py
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/Portal.cpp").read_text()
header = (ROOT / "src/Portal.h").read_text()
config = (ROOT / "src/Config.cpp").read_text()
config_header = (ROOT / "src/Config.h").read_text()


def extract(signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


methods = [extract(signature) for signature in (
    "String Portal::sessionCookie(", "bool Portal::isAuthenticated(",
    "bool Portal::requireAuthentication(", "void Portal::createSession(",
    "void Portal::clearSessions(", "void Portal::sendLoginPage(",
    "void Portal::handleLoginPage(", "void Portal::handleLogin(",
    "void Portal::handleLogout(", "void Portal::onAuthenticated(", "void Portal::begin(",
)]
begin = methods[-1]
# Only login and shared visual resources are public registrations.
assert re.findall(r'_srv\.on\("([^"]+)"', begin) == ["/theme.css", "/theme.js", "/login", "/login"]
assert 'if (!requireAuthentication()) return;' in begin
assert '"Cookie"' in begin
assert "portalPassword" not in extract("void Portal::handleGetConfig(")
assert "authenticate(" not in source  # No username/HTTP Basic login.

password_load = re.search(r'  _portalPassword = .*\n  if \(_portalPassword.*\n', config)[0]
password_save = re.search(r'  _preferences.putString\("portalPass".*\n', config)[0]
password_members = re.search(r'        String _portalPassword = .*', config_header)[0]
password_accessors = "\n".join(line.strip() for line in config_header.splitlines()
                               if "portalPassword()" in line or "void portalPassword(" in line)
post = extract("void Portal::handlePostConfig(")
# Compile its real password validation and real password/save/session operations.
validation = post[post.index('  auto& cfg ='):post.index('  // Valider les réglages radio')]
post_auth = "void Portal::handlePostConfig() {\n" + validation + "\n" + "\n".join(
    line for line in post.splitlines() if any(fragment in line for fragment in (
        "if (changePortalPassword)", "_frisquetManager.config().save();", "_srv.send(200,",
    ))
) + "\n}"
methods.append(post_auth)
handlers = sorted(set(re.findall(r'&Portal::(\w+)', begin)) - {"handleLogout", "handlePostConfig"})
session_members = header[header.index('  static constexpr uint32_t SESSION_DURATION_MS'):header.index('  // Handlers')]

prefix = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

class String {
    char value[1024] = {};
public:
    String() = default;
    String(const char* s) { assert(strlen(s)<sizeof(value)); strcpy(value,s); }
    size_t length() const { return strlen(value); }
    bool isEmpty() const { return !value[0]; }
    const char* c_str() const { return value; }
    bool startsWith(const char* s) const { return strncmp(value,s,strlen(s))==0; }
    int indexOf(char c,int start=0) const { const char* p=strchr(value+start,c);return p?int(p-value):-1; }
    String substring(int start,int end=1023) const {
        if(end>int(length()))end=length();
        if(start>=end)return String();
        char result[1024]={}; memcpy(result,value+start,end-start);return String(result);
    }
    void trim() { int start=0,end=length();while(isspace(value[start]))++start;while(end>start&&isspace(value[end-1]))--end;*this=substring(start,end); }
    void replace(const char* from,const char* to) {
        const char* p=strstr(value,from); if(!p)return;
        *this=substring(0,p-value)+String(to)+substring(p-value+strlen(from));
    }
    bool operator==(const String& s) const { return !strcmp(value,s.value); }
    bool operator!=(const String& s) const { return !(*this==s); }
    friend String operator+(const String& a,const String& b) {
        String r=a;assert(r.length()+b.length()<sizeof(r.value));strcat(r.value,b.value);return r;
    }
};
struct Preferences {
    bool present=false;String stored;
    String getString(const char*,const char* fallback) { return present?stored:String(fallback); }
    void putString(const char*,const String& password) { stored=password;present=true; }
};
'''
config_stub = "class Config { public:\n" + password_members + "\nPreferences _preferences;\n" + password_accessors + "\nvoid load(){\n" + password_load + "}\nvoid save(){\n" + password_save + "}\n};\n"
stubs = r'''
enum HTTPMethod { HTTP_GET, HTTP_POST };
struct Callback {
    void* context=nullptr;void(*invoke)(void*)=nullptr;void(*dispose)(void*)=nullptr;
    ~Callback(){if(dispose)dispose(context);}
    template<class F> void set(F f){
        assert(!context);context=new F(f);
        invoke=[](void* p){(*static_cast<F*>(p))();};
        dispose=[](void* p){delete static_cast<F*>(p);};
    }
    void run(){assert(invoke);invoke(context);}
};
struct WebServer {
    struct Route { String uri;HTTPMethod method=HTTP_GET;Callback handler,upload; } routes[40];
    size_t count=0;Callback notFound;bool cookiesCollected=false;
    String requestUri,cookie,password,newPassword,confirmation,location,cookieOut,cache,body;
    int code=0;HTTPMethod requestMethod=HTTP_GET;
    void collectHeaders(const char** keys,size_t n){cookiesCollected=n==1&&!strcmp(keys[0],"Cookie");}
    template<class F> void on(const char* uri,HTTPMethod method,F f){auto& r=routes[count++];r.uri=uri;r.method=method;r.handler.set(f);}
    template<class F,class G> void on(const char* uri,HTTPMethod method,F f,G g){on(uri,method,f);routes[count-1].upload.set(g);}
    template<class F> void onNotFound(F f){notFound.set(f);}
    void begin(){}
    String uri(){return requestUri;}
    String header(const char* name){return !strcmp(name,"Cookie")?cookie:String();}
    HTTPMethod method(){return requestMethod;}
    String arg(const char* name){
        if(!strcmp(name,"password"))return password;
        if(!strcmp(name,"portalPassword"))return newPassword;
        if(!strcmp(name,"portalPasswordConfirm"))return confirmation;
        return String();
    }
    void sendHeader(const char* name,const String& value){
        if(!strcmp(name,"Location"))location=value;
        if(!strcmp(name,"Set-Cookie"))cookieOut=value;
        if(!strcmp(name,"Cache-Control"))cache=value;
    }
    void send_P(int status,const char* type,const char* content){send(status,type,content);}
    void send(int status,const char* =nullptr,const String& content=String()){code=status;body=content;}
    void prepare(const char* uri,HTTPMethod method,const String& cookies=String()){
        requestUri=uri;requestMethod=method;cookie=cookies;
        code=0;body=String();location=String();cookieOut=String();
        password=String();newPassword=String();confirmation=String();
    }
    Route* find(const char* uri,HTTPMethod method){for(size_t i=0;i<count;++i)if(routes[i].uri==uri&&routes[i].method==method)return &routes[i];return nullptr;}
    void dispatch(){auto* r=find(requestUri.c_str(),requestMethod);if(r)r->handler.run();else notFound.run();}
};
struct FrisquetManager { Config cfg;Config& config(){return cfg;} };
struct Wifi { bool isConnected(){return true;} } WiFi;
uint64_t nowMs=100;uint32_t millis(){return static_cast<uint32_t>(nowMs);}
int64_t esp_timer_get_time(){return nowMs*1000;}
void esp_fill_random(void* buffer,size_t n){static uint8_t seed=1;auto* p=static_cast<uint8_t*>(buffer);for(size_t i=0;i<n;++i)p[i]=seed+i;++seed;}
void info(const char*,...){}
namespace WebAssets { const char* THEME_CSS="theme css"; const char* THEME_JS="theme js"; const char* LOGIN_HTML="<form><input type=password name=password>{{LOGIN_ERROR}}</form>"; }
#define FPSTR(x) x
'''
declarations = "\n".join(method.split("{", 1)[0].replace("Portal::", "", 1).strip() + ";" for method in methods)
declarations = declarations.replace("void (Portal::*uploadHandler)()", "void (Portal::*uploadHandler)() = nullptr")
handler_stubs = "\n".join("void " + name + "(){++handled;}" for name in handlers)
portal = "class Portal { public:\nWebServer _srv;FrisquetManager& _frisquetManager;int handled=0;\n" + session_members + "\nexplicit Portal(FrisquetManager& f):_frisquetManager(f){}\nvoid startAp(){}\n" + declarations + "\n" + handler_stubs + "\n};\n"

checks = r'''
String cookiePair(const String& header){return header.substring(0,header.indexOf(';'));}
String login(Portal& p,const char* password){
    p._srv.prepare("/login",HTTP_POST);p._srv.password=password;p._srv.dispatch();
    assert(p._srv.code==303);assert(p._srv.location=="/");return cookiePair(p._srv.cookieOut);
}
bool authenticated(Portal& p,const String& cookie){p._srv.prepare("/",HTTP_GET,cookie);return p.isAuthenticated();}
int main(){
    Config config;assert(config.portalPassword()=="admin");config.load();assert(config.portalPassword()=="admin");
    config.portalPassword("changed");config.save();config.portalPassword("temporary");config.load();assert(config.portalPassword()=="changed");
    config._preferences.stored="";config.load();assert(config.portalPassword()=="admin");
    FrisquetManager fm;Portal p(fm);p.begin(false);assert(p._srv.cookiesCollected);
    for(size_t i=0;i<p._srv.count;++i){
        auto& r=p._srv.routes[i];if(r.uri=="/login"||r.uri=="/theme.css"||r.uri=="/theme.js")continue;
        p._srv.prepare(r.uri.c_str(),r.method);int before=p.handled;r.handler.run();
        assert(p.handled==before);assert(p._srv.code==(r.uri.startsWith("/api/")?401:303));
    }
    p._srv.prepare("/theme.css",HTTP_GET);p._srv.dispatch();assert(p._srv.code==200&&p._srv.body=="theme css");
    p._srv.prepare("/theme.js",HTTP_GET);p._srv.dispatch();assert(p._srv.code==200&&p._srv.body=="theme js");
    p._srv.prepare("/login",HTTP_GET);p._srv.dispatch();assert(p._srv.code==200);
    for(int i=0;i<5;++i){p._srv.prepare("/login",HTTP_POST);p._srv.password="wrong";p._srv.dispatch();assert(p._srv.code==(i==4?429:401));assert(p._srv.cookieOut.isEmpty());}
    p._srv.prepare("/login",HTTP_POST);p._srv.password="admin";p._srv.dispatch();assert(p._srv.code==429);
    nowMs+=30000;String cookie=login(p,"admin");
    assert(strstr(p._srv.cookieOut.c_str(),"HttpOnly"));assert(strstr(p._srv.cookieOut.c_str(),"SameSite=Strict"));
    assert(authenticated(p,"other=123; "+cookie));assert(!authenticated(p,"ofv_session="));
    assert(!authenticated(p,"fake_ofv_session="+cookie.substring(12)));
    p._srv.prepare("/api/config",HTTP_GET);p._srv.dispatch();assert(p._srv.code==401);
    p._srv.prepare("/api/config",HTTP_GET,cookie);int before=p.handled;p._srv.dispatch();assert(p.handled==before+1);
    p._srv.prepare("/unknown",HTTP_GET);p._srv.dispatch();assert(p._srv.code==303);
    p._srv.prepare("/unknown",HTTP_GET,cookie);p._srv.dispatch();assert(p._srv.code==404);
    // Upload callbacks execute before the final HTTP handler: all are gated.
    auto* update=p._srv.find("/api/update",HTTP_POST);assert(update);
    p._srv.prepare("/api/update",HTTP_POST);before=p.handled;
    for(int i=0;i<4;++i)update->upload.run();update->handler.run();assert(p.handled==before&&p._srv.code==401);
    p._srv.prepare("/api/update",HTTP_POST,cookie);for(int i=0;i<4;++i)update->upload.run();update->handler.run();assert(p.handled==before+5);
    // Confirmation and length validation happen before changing the password.
    p._srv.prepare("/api/config",HTTP_POST,cookie);p._srv.newPassword="changed";p._srv.confirmation="different";p._srv.dispatch();assert(p._srv.code==400&&fm.cfg.portalPassword()=="admin");
    p._srv.prepare("/api/config",HTTP_POST,cookie);p._srv.newPassword="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";p._srv.confirmation=p._srv.newPassword;p._srv.dispatch();assert(p._srv.code==400&&fm.cfg.portalPassword()=="admin");
    p._srv.prepare("/api/config",HTTP_POST,cookie);p._srv.dispatch();assert(p._srv.code==200&&fm.cfg.portalPassword()=="admin");
    p._srv.prepare("/api/config",HTTP_POST,cookie);p._srv.newPassword="changed";p._srv.confirmation="changed";p._srv.dispatch();assert(p._srv.code==200);
    assert(strstr(p._srv.cookieOut.c_str(),"Max-Age=0"));assert(!authenticated(p,cookie));fm.cfg.load();assert(fm.cfg.portalPassword()=="changed");
    p._srv.prepare("/login",HTTP_POST);p._srv.password="admin";p._srv.dispatch();assert(p._srv.code==401);
    cookie=login(p,"changed");String second=login(p,"changed");assert(cookie!=second&&authenticated(p,cookie)&&authenticated(p,second));
    p._srv.prepare("/logout",HTTP_POST,cookie);p._srv.dispatch();assert(!authenticated(p,cookie)&&authenticated(p,second));
    nowMs+=Portal::SESSION_DURATION_MS;assert(!authenticated(p,second));
    // Expiry remains correct across a millis() rollover and after 49 days.
    nowMs=0xFFFFFF00;cookie=login(p,"changed");nowMs+=1000;assert(authenticated(p,cookie));
    nowMs+=(1ULL<<32);assert(!authenticated(p,cookie));
    p.clearSessions();nowMs=1000;cookie=login(p,"changed");
    for(int i=0;i<4;++i){++nowMs;login(p,"changed");}assert(!authenticated(p,cookie));
    puts("OK: password-only login, persistence, protected routes/uploads, sessions, logout, expiry and password changes");
}
'''
with tempfile.TemporaryDirectory(prefix="frisquet-auth-") as temp:
    cpp = Path(temp) / "test.cpp"
    cpp.write_text(prefix + config_stub + stubs + portal + "\n".join(methods) + checks)
    binary = Path(temp) / "test"
    subprocess.run(["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-misleading-indentation", "-fsanitize=address,undefined",
                    str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
