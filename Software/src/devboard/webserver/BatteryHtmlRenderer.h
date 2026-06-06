#ifndef _BATTERY_HTML_RENDERER_H
#define _BATTERY_HTML_RENDERER_H

#include <WString.h>

// Each battery can implement this interface to render more battery specific HTML
// content
class BatteryHtmlRenderer {
 public:
  virtual String get_status_html() = 0;

  // Base URL for the upstream GitHub repository's data folder.
  // Battery renderers can pass this to get_dtc_json_loader_html() directly,
  // or supply their own URL string for a different server/fork.
  static constexpr const char* GITHUB_RAW_BASE_URL =
      "https://raw.githubusercontent.com/dalathegreat/Battery-Emulator/main/data/";

  // Renders a status line + optional file-picker widget + JavaScript that fills
  // DTC descriptions into any table cell carrying a data-dtc-code='<decimal>'
  // attribute.
  //
  // base_url:  Base URL for the JSON file, e.g. GITHUB_RAW_BASE_URL.
  //             Pass "" (default) to skip the GitHub fetch.
  // filename:  Single JSON filename under base_url, e.g. "meb_dtc_003.json"
  //   On a successful GitHub fetch the file picker is hidden automatically.
  //   On failure the file picker is revealed so the user can load a local copy.
  static String get_dtc_json_loader_html(const char* base_url = "", const char* filename = "") {
    String s;
    s += "<div style='margin-top:15px;padding:12px; background: #1e1e2e; border:1px solid #444; border-radius:8px;'>";
    s += "<p id='dtcJsonStatus' style='margin: 0 0 8px 0; color: #aaa; font-size:.95em;'></p>";
    s += "<div id='dtcJsonFileContainer' style='display:none;'>";
    s += "<p style='margin: 4px 0; color: #ccc; font-size:.9em;'><strong>Load DTC descriptions from a local JSON file</strong> ";
    s += "(e.g. <em>";
    s += filename;
    s += "</em>):</p>";
    s += "<input type='file' id='dtcJsonFile' accept='.json' "
         "style='color: #ccc; background: #2a2a3e; border:1px solid #555; border-radius:4px; padding:4px 8px; cursor: pointer;'>";
    s += "</div>";
    s += "</div>";

    s += "<script>";
    s += "(function(){";
    s += "var url='";
    s += base_url;
    s += filename;
    s += "';";
    s += "var cacheKey='dtcJson:'+url;";
    s += "var statusEl=document.getElementById('dtcJsonStatus');";
    s += "var fileContainer=document.getElementById('dtcJsonFileContainer');";
    s += "var fileInput=document.getElementById('dtcJsonFile');";

    s += "function applyDtcs(arr,fromCache){";
    s += "  var map={};arr.forEach(function(e){map[e.code]=e;});";
    s += "  var cells=document.querySelectorAll('[data-dtc-code]');";
    s += "  var matched=0;";
    s += "  cells.forEach(function(td){";
    s += "    var code=parseInt(td.getAttribute('data-dtc-code'),10);";
    s += "    if(map[code]){td.innerHTML=map[code].l_dsc+'<br /><em style=\\'color:#aaa;font-size:0.85em\\'>'+ map[code].s_dsc+'</em>';matched++;}";
    s += "  });";
    s += "  var src=fromCache?' (cached)':' (fetched)';";
    s += "  statusEl.innerHTML='Loaded '+arr.length+' entries, '+matched+'/'+cells.length+' DTCs matched'+src+'.";
    s += " <a href=\\'#\\' id=\\'dtcRefresh\\' style=\\'color:#aaa;font-size:0.85em;\\'>Refresh</a>';";
    s += "  statusEl.style.color=matched>0?'#4CAF50':'#ff9800';";
    s += "  document.getElementById('dtcRefresh').addEventListener('click',function(e){";
    s += "    e.preventDefault();try{localStorage.removeItem(cacheKey);}catch(ex){}fetchFromGitHub();";
    s += "  });";
    s += "}";

    s += "function showFilePicker(msg){";
    s += "  statusEl.textContent=msg;statusEl.style.color='#ff9800';fileContainer.style.display='';";
    s += "}";

    s += "function fetchFromGitHub(){";
    s += "  statusEl.textContent='Fetching DTC descriptions from GitHub...';statusEl.style.color='#aaa';";
    s += "  fetch(url).then(function(r){if(!r.ok)throw new Error('HTTP '+r.status);return r.text();})";
    s += "  .then(function(text){";
    s += "    try{localStorage.setItem(cacheKey,text);}catch(ex){}";
    s += "    applyDtcs(JSON.parse(text),false);";
    s += "  })";
    s += "  .catch(function(err){showFilePicker('GitHub unavailable ('+err.message+') - load from local file:');});";
    s += "}";

    s += "if(url.length>0){";
    s += "  var cached=null;try{cached=localStorage.getItem(cacheKey);}catch(ex){}";
    s += "  if(cached){try{applyDtcs(JSON.parse(cached),true);}catch(ex){fetchFromGitHub();}}";
    s += "  else{fetchFromGitHub();}";
    s += "}else{fileContainer.style.display='';}";
    s += "fileInput.addEventListener('change',function(e){";
    s += "  var file=e.target.files[0];if(!file)return;";
    s += "  statusEl.textContent='Loading...';statusEl.style.color='#aaa';";
    s += "  var reader=new FileReader();";
    s += "  reader.onload=function(ev){";
    s += "    try{applyDtcs(JSON.parse(ev.target.result),false);}";
    s += "    catch(err){statusEl.textContent='Parse error: '+err.message;statusEl.style.color='#d32f2f';}";
    s += "  };";
    s += "  reader.onerror=function(){statusEl.textContent='File read error';statusEl.style.color='#d32f2f';};";
    s += "  reader.readAsText(file);";
    s += "});";
    s += "})();";
    s += "</script>";
    return s;
  }
};

class BatteryDefaultRenderer : public BatteryHtmlRenderer {
 public:
  String get_status_html() { return String("No extra information available for this battery type"); }
};

#endif
