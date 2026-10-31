#include "./editorDocument.h"

// Длинная строка разбита на части: у MSVC ограничена длина одного литерала
const char* const EDITOR_DOCUMENT = R"RML(
<rml>
<head>
<title>SIN-T</title>
<style>
  body, div, wire-layer, knob { display: block; }
  input { display: inline-block; }

  body {
    width: 100%; height: 100%;
    display: flex; flex-direction: column;
    background-color: #14161b; color: #e6e8ec;
    font-family: "Noto Sans"; font-size: 12dp;
  }

  scrollbarvertical { width: 8dp; }
  scrollbarvertical slidertrack { background-color: #14161b; }
  scrollbarvertical sliderbar { background-color: #3a404e; border-radius: 4dp; min-height: 20dp; }
  scrollbarhorizontal { height: 0; }

  #toolbar, #nav {
    display: flex; flex-direction: row; align-items: center;
    flex: 0 0 auto; padding: 0 10dp;
    background-color: #1b1e26;
  }
  #toolbar { height: 40dp; }
  #nav { height: 32dp; border-bottom: 1dp #2b2f3a; background-color: #171a20; }
  #logo { font-size: 15dp; color: #7aa2f7; margin-right: 18dp; }
  .gap { flex: 1 1 auto; }

  .button {
    height: 24dp; line-height: 24dp; padding: 0 10dp; margin-left: 6dp;
    background-color: #2b2f3a; border-radius: 4dp; cursor: pointer;
  }
  .button:hover { background-color: #3a404e; }
  .button:active { background-color: #7aa2f7; color: #14161b; }
  #preset-button { min-width: 180dp; margin-left: 0; }

  #breadcrumbs { display: flex; flex-direction: row; align-items: center; }
  .crumb { color: #8b93a3; cursor: pointer; }
  .crumb:hover { color: #e6e8ec; }
  .crumb.current { color: #e6e8ec; cursor: default; }
  .crumb-separator { color: #5a6172; margin: 0 6dp; }

  #sources { display: flex; flex-direction: row; align-items: center; margin-left: 24dp; }
  .source { position: relative; padding-right: 18dp; color: #ff9e64; }

  #canvas {
    position: relative; flex: 1 1 auto; overflow: hidden; clip: always;
    background-color: #14161b; drag: drag;
  }
  #world { position: absolute; left: 0; top: 0; width: 1dp; height: 1dp; }
  #wires { position: absolute; left: 0; top: 0; width: 100%; height: 100%; pointer-events: none; z-index: 5; }
  #draft { position: absolute; left: 0; top: 0; width: 100%; height: 100%; pointer-events: none; z-index: 20; }

  .block {
    position: absolute; width: 252dp; box-sizing: border-box;
    background-color: #252933; border: 1dp #3a404e; border-radius: 6dp; drag: drag;
  }
  .block:hover { border-color: #5a6172; }
  .block.stub { border-color: #f7768e; }
  .block.small { width: 134dp; }

  .block-header {
    display: flex; flex-direction: row; align-items: center;
    height: 24dp; padding: 0 6dp 0 10dp;
    background-color: #2f3440; border-radius: 6dp 6dp 0 0;
  }
  .block-title { flex: 1 1 auto; white-space: nowrap; overflow: hidden; }
  .block-button {
    width: 16dp; height: 16dp; line-height: 15dp; margin-left: 4dp; text-align: center;
    border-radius: 3dp; color: #8b93a3; cursor: pointer;
  }
  .block-button:hover { background-color: #3a404e; color: #e6e8ec; }
  .power-dot { width: 8dp; height: 8dp; margin: 4dp auto 0; border-radius: 4dp; background-color: #9ece6a; pointer-events: none; }
  .disabled .power-dot { background-color: #5a6172; }

  .block-body { display: flex; flex-direction: row; flex-wrap: wrap; padding: 6dp 4dp; min-height: 28dp; }
  .block-note { padding: 4dp 10dp 8dp; color: #8b93a3; }
  .open-button { margin: 0 10dp 8dp; text-align: center; }

  .knob-cell { position: relative; width: 60dp; height: 72dp; text-align: center; }
  .knob-cell.legal knob { background-color: #4a3423; border-radius: 19dp; }
  knob { width: 38dp; height: 38dp; margin: 0 auto; cursor: pointer; drag: drag; }
  .knob-name { color: #c0c6d2; font-size: 10dp; white-space: nowrap; overflow: hidden; }
  .knob-value { color: #8b93a3; font-size: 10dp; white-space: nowrap; overflow: hidden; }
  .knob-mark {
    position: absolute; right: 8dp; top: 0; width: 6dp; height: 6dp;
    border-radius: 3dp; background-color: #ff9e64;
  }
  .knob-mark.unexported { left: 8dp; right: auto; background-color: #f7768e; }

  .port { position: absolute; width: 14dp; height: 14dp; border-radius: 7dp; drag: drag-drop; cursor: pointer; }
  .port.audio { background-color: #7aa2f7; }
  .port.modulation { background-color: #ff9e64; border-radius: 3dp; }
  .port:hover { background-color: #ffffff; }
  .port.legal { background-color: #9ece6a; }
  .port.input { left: -8dp; }
  .port.output { right: -8dp; }
  .port-label { position: absolute; right: 12dp; color: #ff9e64; font-size: 10dp; pointer-events: none; }
  .source .port { right: 0; top: 3dp; }

  #wire-remove {
    position: absolute; width: 20dp; height: 20dp; line-height: 19dp; text-align: center;
    background-color: #f7768e; color: #14161b; border-radius: 10dp; cursor: pointer; z-index: 10;
  }

  #bottom { display: flex; flex-direction: row; flex: 0 0 188dp; border-top: 1dp #2b2f3a; background-color: #1b1e26; }
  #macros { flex: 0 0 256dp; padding: 6dp; border-right: 1dp #2b2f3a; }
  .panel-title { color: #8b93a3; height: 20dp; line-height: 20dp; }
  #macro-cells { display: flex; flex-direction: row; flex-wrap: wrap; }
  #macro-cells .knob-cell { width: 61dp; height: 74dp; }
  #macro-cells .port { right: 2dp; top: 22dp; }

  #matrix { flex: 1 1 auto; display: flex; flex-direction: column; padding: 6dp 0 6dp 10dp; }
  #matrix-header { display: flex; flex-direction: row; align-items: center; flex: 0 0 26dp; padding-right: 10dp; }
  #matrix-rows { flex: 1 1 auto; overflow: hidden auto; padding-right: 10dp; }
  .route { display: flex; flex-direction: row; align-items: center; height: 24dp; border-bottom: 1dp #252933; }
  .route-source { flex: 0 0 150dp; color: #ff9e64; white-space: nowrap; overflow: hidden; }
  .route-target { flex: 1 1 auto; white-space: nowrap; overflow: hidden; }
  .route-depth { position: relative; flex: 0 0 160dp; height: 10dp; margin: 0 10dp; background-color: #14161b; border-radius: 3dp; drag: drag; cursor: pointer; }
  .route-fill { position: absolute; top: 0; height: 10dp; background-color: #ff9e64; border-radius: 3dp; pointer-events: none; }
  .route-center { position: absolute; left: 50%; top: 0; width: 1dp; height: 10dp; background-color: #5a6172; pointer-events: none; }
  .route-value { flex: 0 0 40dp; text-align: right; color: #c0c6d2; }
  .matrix-empty { padding: 14dp 0; color: #5a6172; }

  #popup {
    position: absolute; min-width: 180dp; max-height: 320dp; overflow: hidden auto; padding: 4dp;
    background-color: #2b2f3a; border: 1dp #4d5668; border-radius: 4dp; z-index: 30;
  }
  .menu-item { height: 24dp; line-height: 24dp; padding: 0 10dp; white-space: nowrap; border-radius: 3dp; cursor: pointer; }
  .menu-item:hover { background-color: #3f4655; }
  .menu-heading { height: 22dp; line-height: 22dp; padding: 0 10dp; color: #8b93a3; }

  #prompt {
    position: absolute; left: 50%; top: 120dp; width: 320dp; margin-left: -160dp; padding: 12dp;
    background-color: #2b2f3a; border: 1dp #4d5668; border-radius: 6dp; z-index: 40;
  }
  #prompt-label { margin-bottom: 8dp; }
  #prompt-input {
    box-sizing: border-box; width: 100%; height: 26dp; line-height: 24dp; padding: 0 6dp; margin-bottom: 10dp;
    background-color: #14161b; border: 1dp #4d5668; color: #e6e8ec; font-size: 12dp;
  }
  #prompt-input:focus { border-color: #7aa2f7; }
  #prompt-buttons { display: flex; flex-direction: row; justify-content: flex-end; }

  #toast {
    position: absolute; left: 50%; bottom: 200dp; width: 480dp; margin-left: -240dp; padding: 8dp 12dp;
    text-align: center; background-color: #3a2a32; border: 1dp #f7768e; border-radius: 4dp; z-index: 50;
  }

  .hidden { display: none; }
</style>
)RML"
R"RML(
</head>
<body>
  <div id="toolbar">
    <div id="logo">SIN-T</div>
    <div id="preset-button" class="button" data-action="presets"></div>
    <div class="button" data-action="save">Save</div>
    <div class="button" data-action="save-as">Save as</div>
    <div class="gap"></div>
  </div>
  <div id="nav">
    <div id="breadcrumbs"></div>
    <div id="sources"></div>
    <div class="gap"></div>
    <div class="button" data-action="add-module">+ Add module</div>
  </div>
  <div id="canvas" data-drag="canvas">
    <div id="world"></div>
    <wire-layer id="wires"></wire-layer>
    <div id="wire-remove" class="hidden" data-action="remove-wire">×</div>
  </div>
  <div id="bottom">
    <div id="macros">
      <div class="panel-title">Macros</div>
      <div id="macro-cells"></div>
    </div>
    <div id="matrix">
      <div id="matrix-header">
        <div class="panel-title">Modulation matrix</div>
        <div class="gap"></div>
        <div class="button" data-action="add-route">+ Add route</div>
      </div>
      <div id="matrix-rows"></div>
    </div>
  </div>
  <wire-layer id="draft"></wire-layer>
  <div id="popup" class="hidden"></div>
  <div id="prompt" class="hidden">
    <div id="prompt-label"></div>
    <input id="prompt-input" type="text"/>
    <div id="prompt-buttons">
      <div class="button" data-action="prompt-cancel">Cancel</div>
      <div class="button" data-action="prompt-accept">OK</div>
    </div>
  </div>
  <div id="toast" class="hidden"></div>
</body>
</rml>
)RML";
