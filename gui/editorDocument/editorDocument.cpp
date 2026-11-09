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
    background-color: #0f1216; color: #d6dbe3;
    font-family: "Noto Sans"; font-size: 12dp;
  }

  scrollbarvertical { width: 8dp; }
  scrollbarvertical slidertrack { background-color: #0c0e12; border-radius: 4dp; }
  scrollbarvertical sliderbar { background-color: #3a414c; border-radius: 4dp; min-height: 20dp; }
  scrollbarhorizontal { height: 0; }

  #toolbar, #nav {
    display: flex; flex-direction: row; align-items: center;
    flex: 0 0 auto; padding: 0 10dp;
  }
  #toolbar {
    height: 44dp;
    decorator: linear-gradient(180deg, #343a43 0%, #23272e 55%, #1b1e24 100%);
    border-top: 1dp #4a525e; border-bottom: 1dp #0a0c0f;
  }
  #nav {
    height: 32dp;
    decorator: linear-gradient(180deg, #1d2127 0%, #171a1f 100%);
    border-top: 1dp #2c3139; border-bottom: 1dp #07080a;
  }
  #logo {
    font-size: 20dp; color: #c4cedb; margin-right: 18dp;
    font-effect: shadow(0dp 1dp #000000c0);
  }
  .gap { flex: 1 1 auto; }

  .button {
    height: 24dp; line-height: 22dp; padding: 0 12dp; margin-left: 6dp; box-sizing: border-box;
    decorator: linear-gradient(180deg, #424954 0%, #2c3139 100%);
    border: 1dp #15181c; border-top-color: #5c6573; border-radius: 3dp;
    box-shadow: #00000080 0dp 1dp 2dp;
    color: #d6dbe3; cursor: pointer;
  }
  .button:hover { decorator: linear-gradient(180deg, #4d5562 0%, #343a44 100%); color: #ffffff; }
  .button:active {
    decorator: linear-gradient(180deg, #1f4f8c 0%, #2a66b0 100%);
    border-top-color: #12305a; color: #ffffff;
  }
  #preset-button {
    min-width: 200dp; margin-left: 0;
    decorator: linear-gradient(180deg, #0b0d10 0%, #14181d 100%);
    border: 1dp #3a414c; border-top-color: #050607; border-radius: 3dp;
    box-shadow: #000000a0 0dp 1dp 3dp inset;
    color: #9fc4f5;
  }
  #preset-button:hover { decorator: linear-gradient(180deg, #0e1115 0%, #191e24 100%); }

  #breadcrumbs { display: flex; flex-direction: row; align-items: center; }
  .crumb { color: #7f8896; cursor: pointer; }
  .crumb:hover { color: #d6dbe3; }
  .crumb.current { color: #d6dbe3; cursor: default; }
  .crumb-separator { color: #4c5361; margin: 0 6dp; }

  #sources { display: flex; flex-direction: row; align-items: center; margin-left: 24dp; }
  .source { position: relative; padding-right: 18dp; color: #f0923a; }

  #canvas {
    position: relative; flex: 1 1 auto; overflow: hidden; clip: always;
    background-color: #0d1014; drag: drag;
    decorator: radial-gradient(circle farthest-corner at 50% 40%, #18202a 0%, #0f1318 60%, #0a0c0f 100%);
    box-shadow: #000000d0 0dp 2dp 8dp inset;
  }
  #world { position: absolute; left: 0; top: 0; width: 1dp; height: 1dp; }
  #wires { position: absolute; left: 0; top: 0; width: 100%; height: 100%; pointer-events: none; z-index: 5; }
  #draft { position: absolute; left: 0; top: 0; width: 100%; height: 100%; pointer-events: none; z-index: 20; }

  .block {
    position: absolute; width: 252dp; box-sizing: border-box;
    decorator: linear-gradient(180deg, #2c3139 0%, #22262d 45%, #1b1e23 100%);
    border: 1dp #0b0d10; border-top-color: #4a525e; border-radius: 4dp; drag: drag;
    box-shadow: #000000b0 0dp 4dp 10dp, #ffffff0c 0dp 0dp 0dp 1dp inset;
  }
  .block:hover { border-top-color: #66707e; box-shadow: #000000c0 0dp 5dp 12dp, #ffffff14 0dp 0dp 0dp 1dp inset; }
  .block.stub { border-color: #c0485c; }
  .block.small { width: 134dp; }

  .block-header {
    display: flex; flex-direction: row; align-items: center;
    height: 24dp; padding: 0 6dp 0 10dp;
    decorator: linear-gradient(180deg, #3b414b 0%, #2b3038 100%);
    border-bottom: 1dp #0d0f12; border-radius: 3dp 3dp 0 0;
  }
  .block-title { flex: 1 1 auto; white-space: nowrap; overflow: hidden; color: #e2e6ec; font-effect: shadow(0dp 1dp #000000a0); }
  .block-button {
    width: 16dp; height: 16dp; line-height: 15dp; margin-left: 4dp; text-align: center;
    border-radius: 3dp; color: #8a93a1; cursor: pointer;
  }
  .block-button:hover { background-color: #4a525e; color: #ffffff; }
  .power-dot {
    width: 8dp; height: 8dp; margin: 4dp auto 0; border-radius: 4dp; pointer-events: none;
    background-color: #7fd36a; box-shadow: #7fd36a60 0dp 0dp 4dp;
  }
  .disabled .power-dot { background-color: #3c424c; box-shadow: none; }

  .block-body { display: flex; flex-direction: row; flex-wrap: wrap; padding: 6dp 4dp; min-height: 28dp; }
  .block-note { padding: 4dp 10dp 8dp; color: #7f8896; }
  .open-button { margin: 0 10dp 8dp; text-align: center; }

)RML"
R"RML(
  .knob-cell { position: relative; width: 60dp; height: 72dp; text-align: center; }
  .knob-cell.legal knob { background-color: #4a3423; border-radius: 19dp; }
  knob { width: 38dp; height: 38dp; margin: 0 auto; cursor: pointer; drag: drag; }
  .knob-name { color: #c3cad5; font-size: 10dp; white-space: nowrap; overflow: hidden; }
  .knob-value { color: #7f8896; font-size: 10dp; white-space: nowrap; overflow: hidden; }
  .knob-mark {
    position: absolute; right: 8dp; top: 0; width: 6dp; height: 6dp;
    border-radius: 3dp; background-color: #f0923a;
  }
  .knob-mark.unexported { left: 8dp; right: auto; background-color: #e0566c; }

  .port {
    position: absolute; width: 14dp; height: 14dp; border-radius: 7dp;
    box-shadow: #0a0c0f 0dp 0dp 0dp 2dp, #00000080 0dp 2dp 3dp 2dp;
    drag: drag-drop; cursor: pointer;
  }
  .port.audio { background-color: #3d8de8; }
  .port.modulation { background-color: #f0923a; border-radius: 3dp; }
  .port:hover { background-color: #ffffff; }
  .port.legal { background-color: #7fd36a; }
  .port.input { left: -8dp; }
  .port.output { right: -8dp; }
  .port-label { position: absolute; right: 12dp; color: #f0923a; font-size: 10dp; pointer-events: none; }
  .source .port { right: 0; top: 3dp; }

  #wire-remove {
    position: absolute; width: 20dp; height: 20dp; line-height: 19dp; text-align: center;
    background-color: #e0566c; color: #0f1216; border-radius: 10dp; cursor: pointer; z-index: 10;
  }

  #bottom {
    display: flex; flex-direction: row; flex: 0 0 188dp; padding: 6dp;
    decorator: linear-gradient(180deg, #2a2f36 0%, #1c1f24 100%);
    border-top: 1dp #4a525e;
  }
  #macros, #matrix {
    decorator: linear-gradient(180deg, #1c2026 0%, #15181c 100%);
    border: 1dp #3a414c; border-top-color: #07080a; border-radius: 4dp;
    box-shadow: #000000b0 0dp 2dp 6dp inset;
  }
  #macros { flex: 0 0 256dp; padding: 6dp; margin-right: 6dp; }
  .panel-title { color: #aeb8c6; height: 20dp; line-height: 20dp; font-effect: shadow(0dp 1dp #000000c0); }
  #matrix-header .panel-title { color: #6fa8ec; font-size: 14dp; }
  #macro-cells { display: flex; flex-direction: row; flex-wrap: wrap; }
  #macro-cells .knob-cell { width: 61dp; height: 74dp; }
  #macro-cells .port { right: 2dp; top: 22dp; }

  #matrix { flex: 1 1 auto; display: flex; flex-direction: column; padding: 6dp 0 6dp 10dp; }
  #matrix-header { display: flex; flex-direction: row; align-items: center; flex: 0 0 26dp; padding-right: 10dp; }
  #matrix-rows { flex: 1 1 auto; overflow: hidden auto; padding-right: 10dp; }
  .route {
    display: flex; flex-direction: row; align-items: center; height: 26dp; margin-bottom: 3dp;
    decorator: linear-gradient(180deg, #1f242b 0%, #191d22 100%);
    border: 1dp #0a0c0f; border-top-color: #2e343d; border-radius: 3dp;
  }
  .route-source { flex: 0 0 150dp; padding-left: 8dp; color: #f0923a; white-space: nowrap; overflow: hidden; }
  .route-target { flex: 1 1 auto; white-space: nowrap; overflow: hidden; }
  .route-depth {
    position: relative; flex: 0 0 160dp; height: 10dp; margin: 0 10dp;
    background-color: #08090b; border-radius: 2dp;
    box-shadow: #2c3139 0dp 1dp 0dp 1dp, #000000 0dp 0dp 0dp 1dp;
    drag: drag; cursor: pointer;
  }
  .route-fill {
    position: absolute; top: 0; height: 100%; border-radius: 1dp; pointer-events: none;
    decorator: linear-gradient(180deg, #5aa6ff 0%, #2a6fc4 100%);
    box-shadow: #3d8de840 0dp 0dp 4dp;
  }
  .route-center { position: absolute; left: 50%; top: 0; width: 1dp; height: 100%; background-color: #4c5361; pointer-events: none; }
  .route-value { flex: 0 0 40dp; text-align: right; color: #c3cad5; }
  .matrix-empty { padding: 14dp 0; color: #4c5361; }

  #popup {
    position: absolute; min-width: 180dp; max-height: 320dp; overflow: hidden auto; padding: 4dp;
    decorator: linear-gradient(180deg, #2c3139 0%, #1f2329 100%);
    border: 1dp #0a0c0f; border-top-color: #565f6c; border-radius: 4dp; z-index: 30;
    box-shadow: #000000c0 0dp 6dp 16dp;
  }
  .menu-item { height: 24dp; line-height: 24dp; padding: 0 10dp; white-space: nowrap; border-radius: 3dp; cursor: pointer; }
  .menu-item:hover { background-color: #2a5ea8; color: #ffffff; }
  .menu-heading { height: 22dp; line-height: 22dp; padding: 0 10dp; color: #7f8896; }

  #prompt {
    position: absolute; left: 50%; top: 120dp; width: 320dp; margin-left: -160dp; padding: 12dp;
    decorator: linear-gradient(180deg, #2c3139 0%, #1f2329 100%);
    border: 1dp #0a0c0f; border-top-color: #565f6c; border-radius: 4dp; z-index: 40;
    box-shadow: #000000c0 0dp 8dp 20dp;
  }
  #prompt-label { margin-bottom: 8dp; }
  #prompt-input {
    box-sizing: border-box; width: 100%; height: 26dp; line-height: 24dp; padding: 0 6dp; margin-bottom: 10dp;
    background-color: #0b0d10; border: 1dp #3a414c; border-top-color: #050607; color: #d6dbe3; font-size: 12dp;
  }
  #prompt-input:focus { border-color: #3d8de8; }
  #prompt-buttons { display: flex; flex-direction: row; justify-content: flex-end; }

  #toast {
    position: absolute; left: 50%; bottom: 200dp; width: 480dp; margin-left: -240dp; padding: 8dp 12dp;
    text-align: center; background-color: #2a1d22; border: 1dp #e0566c; border-radius: 4dp; z-index: 50;
    box-shadow: #000000c0 0dp 6dp 16dp;
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
