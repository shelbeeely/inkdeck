#include "SetupServer.h"

#include <WebServer.h>
#include <WiFi.h>

#include "Clock.h"
#include "Config.h"

namespace SetupServer {

namespace {

WebServer* g_server = nullptr;
bool g_saved = false;

String htmlEscape(const char* s) {
  String out;
  for (; *s; ++s) {
    switch (*s) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += *s;
    }
  }
  return out;
}

const char kHead[] = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>InkDeck setup</title>
<style>
body{font:16px/1.45 system-ui,sans-serif;margin:0;padding:16px;background:#f6f6f4;color:#1b1b1b}
main{max-width:520px;margin:0 auto}
h1{font-size:1.4em;margin:.2em 0 .6em}
label{display:block;font-weight:600;margin:14px 0 4px}
input,select{width:100%;box-sizing:border-box;padding:10px;font:inherit;border:1px solid #999;border-radius:8px;background:#fff}
small{display:block;color:#555;margin-top:4px}
button{margin-top:20px;width:100%;padding:12px;font:inherit;font-weight:600;border:0;border-radius:8px;background:#1b1b1b;color:#fff}
details{margin-top:14px}.ok{padding:12px;border-radius:8px;background:#e3f3e3}.warn{padding:12px;border-radius:8px;background:#fdf0d5;margin-top:10px}
a{color:#0b57d0}
</style></head><body><main>
)HTML";

const char kTail[] = R"HTML(</main></body></html>)HTML";

// IANA zone -> POSIX TZ string, for preselecting the browser's time zone.
const char kScript[] = R"HTML(<script>
const Z=[["UTC","UTC0"],["Europe/London","GMT0BST,M3.5.0/1,M10.5.0"],["Europe/Dublin","IST-1GMT0,M10.5.0,M3.5.0/1"],
["Europe/Lisbon","WET0WEST,M3.5.0/1,M10.5.0"],["Europe/Paris","CET-1CEST,M3.5.0,M10.5.0/3"],["Europe/Berlin","CET-1CEST,M3.5.0,M10.5.0/3"],
["Europe/Madrid","CET-1CEST,M3.5.0,M10.5.0/3"],["Europe/Rome","CET-1CEST,M3.5.0,M10.5.0/3"],["Europe/Amsterdam","CET-1CEST,M3.5.0,M10.5.0/3"],
["Europe/Brussels","CET-1CEST,M3.5.0,M10.5.0/3"],["Europe/Zurich","CET-1CEST,M3.5.0,M10.5.0/3"],["Europe/Vienna","CET-1CEST,M3.5.0,M10.5.0/3"],
["Europe/Stockholm","CET-1CEST,M3.5.0,M10.5.0/3"],["Europe/Oslo","CET-1CEST,M3.5.0,M10.5.0/3"],["Europe/Copenhagen","CET-1CEST,M3.5.0,M10.5.0/3"],
["Europe/Warsaw","CET-1CEST,M3.5.0,M10.5.0/3"],["Europe/Prague","CET-1CEST,M3.5.0,M10.5.0/3"],["Europe/Budapest","CET-1CEST,M3.5.0,M10.5.0/3"],
["Europe/Athens","EET-2EEST,M3.5.0/3,M10.5.0/4"],["Europe/Helsinki","EET-2EEST,M3.5.0/3,M10.5.0/4"],["Europe/Kyiv","EET-2EEST,M3.5.0/3,M10.5.0/4"],
["Europe/Kiev","EET-2EEST,M3.5.0/3,M10.5.0/4"],["Europe/Bucharest","EET-2EEST,M3.5.0/3,M10.5.0/4"],["Europe/Istanbul","<+03>-3"],
["Europe/Moscow","MSK-3"],["Africa/Lagos","WAT-1"],["Africa/Johannesburg","SAST-2"],["Africa/Cairo","EET-2EEST,M4.5.5/0,M10.5.4/24"],
["Africa/Nairobi","EAT-3"],["Asia/Jerusalem","IST-2IDT,M3.4.5/2,M10.5.0"],["Asia/Riyadh","<+03>-3"],["Asia/Tehran","<+0330>-3:30"],
["Asia/Dubai","<+04>-4"],["Asia/Karachi","PKT-5"],["Asia/Kolkata","IST-5:30"],["Asia/Calcutta","IST-5:30"],["Asia/Kathmandu","<+0545>-5:45"],
["Asia/Dhaka","<+06>-6"],["Asia/Bangkok","<+07>-7"],["Asia/Ho_Chi_Minh","<+07>-7"],["Asia/Jakarta","WIB-7"],["Asia/Shanghai","CST-8"],
["Asia/Hong_Kong","HKT-8"],["Asia/Taipei","CST-8"],["Asia/Singapore","<+08>-8"],["Asia/Manila","PST-8"],["Australia/Perth","AWST-8"],
["Asia/Seoul","KST-9"],["Asia/Tokyo","JST-9"],["Australia/Darwin","ACST-9:30"],["Australia/Adelaide","ACST-9:30ACDT,M10.1.0,M4.1.0/3"],
["Australia/Brisbane","AEST-10"],["Australia/Sydney","AEST-10AEDT,M10.1.0,M4.1.0/3"],["Australia/Melbourne","AEST-10AEDT,M10.1.0,M4.1.0/3"],
["Australia/Hobart","AEST-10AEDT,M10.1.0,M4.1.0/3"],["Pacific/Auckland","NZST-12NZDT,M9.5.0,M4.1.0/3"],["Pacific/Honolulu","HST10"],
["America/Anchorage","AKST9AKDT,M3.2.0,M11.1.0"],["America/Los_Angeles","PST8PDT,M3.2.0,M11.1.0"],["America/Vancouver","PST8PDT,M3.2.0,M11.1.0"],
["America/Phoenix","MST7"],["America/Denver","MST7MDT,M3.2.0,M11.1.0"],["America/Edmonton","MST7MDT,M3.2.0,M11.1.0"],
["America/Chicago","CST6CDT,M3.2.0,M11.1.0"],["America/Winnipeg","CST6CDT,M3.2.0,M11.1.0"],["America/Mexico_City","CST6"],
["America/New_York","EST5EDT,M3.2.0,M11.1.0"],["America/Detroit","EST5EDT,M3.2.0,M11.1.0"],["America/Toronto","EST5EDT,M3.2.0,M11.1.0"],
["America/Bogota","<-05>5"],["America/Lima","<-05>5"],["America/Halifax","AST4ADT,M3.2.0,M11.1.0"],["America/Santiago","<-04>4<-03>,M9.1.6/24,M4.1.6/24"],
["America/Sao_Paulo","<-03>3"],["America/Argentina/Buenos_Aires","<-03>3"],["America/St_Johns","NST3:30NDT,M3.2.0,M11.1.0"]];
const f=document.forms.cfg,sel=f.tzsel,tz=f.timezone,cur=f.dataset.tz;
let browser="";try{browser=Intl.DateTimeFormat().resolvedOptions().timeZone||""}catch(e){}
for(const [n,p] of Z){const o=new Option(n.replace(/_/g," "),p);o.dataset.zone=n;sel.add(o)}
sel.add(new Option("Other (POSIX TZ string)",""));
function pick(){const z=[...sel.options].find(o=>o.dataset.zone===browser);
 if(cur&&cur!=="UTC0"){const m=[...sel.options].find(o=>o.value===cur);if(m){sel.value=cur}else{sel.value="";tz.value=cur}}
 else if(z){z.selected=true}else{sel.value="UTC0"}}
pick();function sync(){const other=sel.value==="";tz.hidden=!other;if(!other)tz.value=sel.value}
sel.onchange=sync;sync();
const tok=document.getElementById("toklink");
function link(){const u=f.repo_url.value;tok.hidden=!/github\.com/i.test(u)}
f.repo_url.oninput=link;link();
</script>)HTML";

void handleRoot() {
  const Config& c = g_config;
  String page;
  page.reserve(9000);
  page += kHead;
  page += "<h1>InkDeck setup</h1><p>Connected to <b>";
  page += htmlEscape(WiFi.SSID().c_str());
  page += "</b>. Settings are saved on the device.</p>";
  page += "<form name=cfg method=post action=/save data-tz=\"";
  page += htmlEscape(c.timezone);
  page += "\"><label for=r>GitJournal repository</label><input id=r name=repo_url autocomplete=off "
          "placeholder=\"git@github.com:you/journal.git\" value=\"";
  page += htmlEscape(c.repoUrl);
  page += "\"><small>In GitJournal: Settings &rarr; Git Remote Settings &rarr; Git Host. SSH or https URL.</small>";
  page += "<label for=t>Access token</label><input id=t name=git_token type=password autocomplete=off placeholder=\"";
  page += c.token[0] ? "Saved &mdash; leave empty to keep it" : "github_pat_...";
  page += "\"><small id=toklink>GitHub: <a href=\"https://github.com/settings/personal-access-tokens/new\" "
          "target=_blank rel=noopener>create a fine-grained token</a> for this repository only, with "
          "<b>Contents: Read and write</b>.</small>";
  page += "<label for=b>Branch</label><input id=b name=branch value=\"";
  page += htmlEscape(c.branch);
  page += "\"><label for=z>Time zone</label><select id=z name=tzsel></select>"
          "<input name=timezone hidden placeholder=\"e.g. CET-1CEST,M3.5.0,M10.5.0/3\">";
  page += "<details><summary>Folders</summary><label for=n>Notes folder on the SD card</label><input id=n "
          "name=notes_folder value=\"";
  page += htmlEscape(c.notesFolder);
  page += "\"><label for=j>Journal subfolder (empty = top level)</label><input id=j name=journal_subfolder value=\"";
  page += htmlEscape(c.journalSubfolder);
  page += "\"></details><button>Save</button></form>";
  page += kScript;
  page += kTail;
  g_server->send(200, "text/html; charset=utf-8", page);
}

// One "key = value" line from a form field (line breaks dropped).
void addLine(String& lines, const char* key, const String& value) {
  String v = value;
  v.replace("\r", "");
  v.replace("\n", "");
  v.trim();
  lines += key;
  lines += " = ";
  lines += v;
  lines += '\n';
}

void handleSave() {
  String lines;
  addLine(lines, "repo_url", g_server->arg("repo_url"));
  addLine(lines, "branch", g_server->arg("branch"));
  addLine(lines, "timezone", g_server->arg("timezone"));
  addLine(lines, "notes_folder", g_server->arg("notes_folder"));
  // Empty means "top level" here, which "/" spells (empty would fall back).
  String sub = g_server->arg("journal_subfolder");
  sub.trim();
  addLine(lines, "journal_subfolder", sub.length() ? sub : String("/"));
  if (g_server->arg("git_token").length()) addLine(lines, "git_token", g_server->arg("git_token"));

  const bool ok = g_config.storeOnDevice(lines.c_str());
  Clock::applyTimezone(g_config.timezone);

  String page;
  page += kHead;
  page += "<h1>InkDeck setup</h1>";
  if (!ok) {
    page += "<p class=warn>Couldn't save the settings. Try again.</p>";
  } else {
    page += "<p class=ok>Saved. On the InkDeck, press <b>OK</b> to sync now, or <b>Back</b> to finish.</p>";
    if (!g_config.hasRepo()) {
      page += "<p class=warn>The repository URL couldn't be read.</p>";
    } else if (!g_config.repoIsGitHub()) {
      page += "<p class=warn>";
      page += htmlEscape(g_config.repoHost);
      page += " is saved, but only GitHub syncs so far.</p>";
    } else if (!g_config.token[0]) {
      page += "<p class=warn>No access token yet: Sync needs one.</p>";
    }
    const char* keys[] = {"repo_url", "git_token", "branch", "timezone", "notes_folder", "journal_subfolder"};
    for (const char* k : keys) {
      if (g_config.sdOverrides(k)) {
        page += "<p class=warn>/inkdeck.txt on the SD card sets <b>";
        page += k;
        page += "</b>, which overrides what you saved here.</p>";
      }
    }
  }
  page += "<p><a href=/>Back to the settings</a></p>";
  page += kTail;
  g_server->send(200, "text/html; charset=utf-8", page);
  if (ok) g_saved = true;
}

}  // namespace

void start() {
  if (g_server) return;
  g_saved = false;
  g_server = new WebServer(80);
  g_server->on("/", HTTP_GET, handleRoot);
  g_server->on("/save", HTTP_POST, handleSave);
  g_server->onNotFound([] {
    g_server->sendHeader("Location", "/");
    g_server->send(302, "text/plain", "");
  });
  g_server->begin();
}

void stop() {
  if (!g_server) return;
  g_server->stop();
  delete g_server;
  g_server = nullptr;
}

void poll() {
  if (g_server) g_server->handleClient();
}

bool takeSaved() {
  const bool s = g_saved;
  g_saved = false;
  return s;
}

}  // namespace SetupServer
