#include "setup_page.h"

#include <cstdio>

PageText &PageText::operator+=(int number)
{
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", number);
  return *this += buf;
}

void page_escape(PageText &p, const char *text)
{
  for (const char *c = text; *c; c++) {
    switch (*c) {
    case '&': p += "&amp;"; break;
    case '<': p += "&lt;"; break;
    case '>': p += "&gt;"; break;
    case '"': p += "&quot;"; break;
    case '\'': p += "&#39;"; break;
    default: {
      const char one[2] = {*c, 0};
      p += one;
    }
    }
  }
}

void setup_page(PageText &p, const PageData &d)
{
  p += ("<!doctype html><html><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<meta name='color-scheme' content='light dark'><title>Bambuddy Panel");
  if (d.printerName[0]) {
    p += ": ";
    page_escape(p, d.printerName);
  }
  p += ("</title><style>"
         "body{font-family:system-ui,sans-serif;max-width:30em;margin:1.5em auto;padding:0 1em;color:#222}"
         "label{display:block;margin-top:1em;font-weight:600}"
         "input{width:100%;box-sizing:border-box;padding:.55em;font-size:1em;border:1px solid #bbb;border-radius:6px}"
         "button{margin-top:1.2em;padding:.65em 1.3em;font-size:1em;border-radius:6px;border:0;background:#1f6feb;color:#fff}"
         "button.s{background:#eee;color:#222}small{display:block;color:#666;margin-top:.35em}"
         ".m{padding:.7em;border-radius:6px;background:#eef2ff;margin:1em 0}.e{background:#fde8e8}.ok{background:#e3f6e5}"
         "h1 small{display:inline;font-size:.6em;font-weight:400}"
         "@media(prefers-color-scheme:dark){body{background:#14181c;color:#e6e6e6}"
         "input,select{background:#1f252b;color:#e6e6e6;border-color:#555}button.s{background:#2c343b;color:#e6e6e6}"
         "small{color:#9aa7b0}.m{background:#1d2a44}.e{background:#4a1f1f}.ok{background:#1d3a22}a{color:#8ab4ff}}"
         "</style></head><body><h1>Bambuddy Panel");
  if (d.printerName[0]) { // which panel this is: with one panel to a printer, the pages look alike
    p += " <small>for ";
    page_escape(p, d.printerName);
    p += "</small>";
  }
  p += "</h1>";
  if (d.msg[0]) {
    p += "<div class='m ";
    p += d.kind;
    p += "'>";
    p += d.msg; // built by setup_web.cpp; values in it are escaped
    p += "</div>";
  }
  if (d.done) { // nothing left to fill in: a form here would only invite doing it again
    p += "</body></html>";
    return;
  }
  p += ("<form id='f' method='post' action='/save' "
         "onsubmit=\"var b=this.querySelector('button');b.disabled=true;b.textContent='Checking with Bambuddy...'\">"
         "<label for='code'>Code on the panel's screen</label>"
         "<input id='code' name='code' autocomplete='off' autocapitalize='off' spellcheck='false' onchange='chk()' value='");
  page_escape(p, d.typedCode);
  p += ("'><label for='url'>Bambuddy address</label>"
         "<input id='url' name='url' inputmode='url' autocapitalize='off' spellcheck='false' onchange='chk()' "
         "placeholder='http://192.168.1.50:3700' value='");
  page_escape(p, d.url);
  p += ("'><label for='key'>API key</label>"
         "<input id='key' name='key' type='password' autocomplete='off' placeholder='");
  p += d.keySaved ? ("saved; leave blank to keep it") : ("from Bambuddy's settings");
  p += ("' onchange='chk()'>"
         "<small id='kst'></small><small>Kept only for the address it is saved with: a new address needs "
         "the key again. See <a href='#keyhelp'>how to make one</a>.</small>");
  if (d.choiceCount) {
    p += ("<label id='plabel' for='printer'>Printer</label><select id='printer' name='printer' "
           "style='width:100%;padding:.55em;font-size:1em'>");
    if (d.printerId <= 0) p += ("<option value=''>Choose...</option>");
    for (int i = 0; i < d.choiceCount; i++) {
      p += "<option value='";
      p += d.choices[i].id;
      p += d.choices[i].id == d.printerId ? "' selected>" : "'>";
      page_escape(p, d.choices[i].name);
      p += " (id ";
      p += d.choices[i].id;
      p += ")</option>";
    }
    p += ("</select>");
  } else {
    p += ("<label id='plabel' for='printer'>Printer id</label><input id='printer' name='printer' inputmode='numeric' value='");
    if (d.printerId > 0) p += d.printerId;
    p += ("'><small id='phint'>With the code, address and key filled in, this becomes a list of Bambuddy's printers.</small>");
  }
  p += ("<button>Save</button></form><hr style='margin-top:2em'>");
  // Away from Save, and worded as the exception it is.
  if (d.finishAnyway)
    p += ("<form method='post' action='/finish' onsubmit=\"this.code.value=document.getElementById('code').value\">"
           "<input type='hidden' name='code'><small>Only if the settings are right and Bambuddy is just not "
           "running at the moment:</small><button class='s' style='margin-top:.5em'>Use these settings anyway</button>"
           "</form><hr style='margin-top:2em'>");
  p += ("<form method='post' action='/wifi' "
         "onsubmit=\"this.code.value=document.getElementById('code').value;"
         "return confirm('Forget this WiFi? The panel restarts and opens its WiFi setup network.')\">"
         "<input type='hidden' name='code'><button class='s'>Change WiFi</button></form>");
  p += ("<h2 id='keyhelp' style='margin-top:2em'>Making the API key</h2>"
         "<ol><li>In Bambuddy, open <b>Settings &gt; API keys</b> and create a key for this panel.</li>"
         "<li>Bambuddy ticks most permissions for a new key. Untick all but these:"
         "<table><tr><th align='left'>Permission</th><th align='left'>For</th></tr>"
         "<tr><td>Read Status</td><td>everything the panel shows</td></tr>"
         "<tr><td>Control Printer</td><td>pause, resume, stop, plate is clear, clear error</td></tr>"
         "<tr><td>Manage Queue</td><td>start the next job, resume the queue</td></tr>"
         "<tr><td>Manage Archives <small>(optional)</small></td><td>answering whether a print came out well</td></tr>"
         "<tr><td>Manage Maintenance <small>(optional)</small></td><td>marking maintenance done</td></tr>"
         "<tr><td>Manage Inventory <small>(optional)</small></td><td>assigning a spool to a slot</td></tr>"
         "</table></li>"
         "<li>Copy the key and paste it above.</li></ol>"
         "<details><summary>What the key's owner needs too</summary>"
         "<p>A key never does more than the Bambuddy user who owns it may do:</p><ul>"
         "<li><b>Resume queue</b> needs an owner who may manage <i>all</i> queue jobs.</li>"
         "<li><b>Start</b> works on the owner's own jobs, or on anyone's if the owner may manage all "
         "queue jobs. Jobs waiting for review need an owner who may start unreviewed jobs.</li>"
         "<li><b>Answering the outcome question</b> works on the owner's own prints, or on anyone's if "
         "the owner may edit all archives.</li></ul>"
         "<p>Without a permission the panel says which one the key lacks; the rest works.</p>"
         "</details>"
         "<details><summary>Limiting the key to this printer</summary>"
         "<p>Bambuddy's settings page can't do this (1.3.x), its API can: "
         "<code>PATCH /api/v1/api-keys/{key_id}</code> with the body "
         "<code>{\"printer_ids\": [&lt;printer id&gt;]}</code>.</p></details>"
         "<p><small>The key is stored in the panel's flash, which isn't encrypted, and travels over plain "
         "HTTP on your network. Delete it in Bambuddy if the panel goes missing.</small></p>");
  // Checks the key without saving anything, and turns the printer field into Bambuddy's list.
  // Text goes in with textContent, so nothing Bambuddy sends can become markup.
  p += ("<script>function chk(){var s=document.getElementById('kst'),f=document.getElementById('f');"
         // Nothing to check until the code, the address and a key (typed, or the saved one) are there.
         "s.style.color='';if(!f.code.value||!f.url.value||(!f.key.value&&f.key.placeholder.indexOf('saved')))"
         "{s.textContent='';return}"
         "s.textContent='Checking with Bambuddy...';"
         "fetch('/check',{method:'POST',body:new URLSearchParams({code:f.code.value,url:f.url.value,key:f.key.value})})"
         ".then(function(r){return r.json()}).then(function(j){s.textContent=j.msg;s.style.color=j.ok?'#1a7f37':'#b42318';"
         "if(!j.ok||!j.printers.length)return;var o=document.getElementById('printer'),n=document.createElement('select');"
         "n.id='printer';n.name='printer';n.style.cssText='width:100%;padding:.55em;font-size:1em';"
         "var cur=parseInt(o.value)||j.selected;if(j.printers.length>1){var c=document.createElement('option');"
         "c.value='';c.textContent='Choose...';n.appendChild(c)}"
         "j.printers.forEach(function(p){var e=document.createElement('option');e.value=p.id;"
         "e.textContent=p.name+' (id '+p.id+')';if(p.id==cur||j.printers.length==1)e.selected=true;n.appendChild(e)});"
         "o.replaceWith(n);document.getElementById('plabel').textContent='Printer';"
         "var h=document.getElementById('phint');if(h)h.remove()})"
         ".catch(function(){s.textContent='The panel didn\\'t answer.';s.style.color='#b42318'})}chk()</script>");
  p += ("</body></html>");
}
