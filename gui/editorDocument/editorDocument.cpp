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
  /*
    Металл выпуклой пластины — три слоя, первый рисуется сверху:
    блик полосами, основной тон, кромка. Кромка закрашивает всю пластину вместе с рамкой,
    два верхних слоя закрывают всё, кроме рамки, и от кромки остаётся градиентная рамка.
    Свет падает сверху слева. Внутренняя тень (inset) рисуется под слоями,
    поэтому у углублённых панелей тон задан фоном, а слои полупрозрачные.
  */
  #toolbar {
    height: 44dp;
    border-top: 1dp #000000;
    decorator:
      linear-gradient(100deg, #ffffff00 0%, #ffffff16 16%, #ffffff00 32%, #00000018 46%, #ffffff00 56%, #ffffff10 72%, #ffffff00 88%) padding-box,
      image(sint:worn repeat) padding-box,
      linear-gradient(180deg, #3b424c 0%, #2a2e35 55%, #1e2127 100%) padding-box,
      linear-gradient(90deg, #a3aebd 0%, #5d6672 35%, #3a414b 100%) border-box;
  }
  #nav {
    height: 32dp;
    background-color: #181b20;
    decorator: linear-gradient(180deg, #ffffff06 0%, #00000020 100%), image(sint:scuffed repeat);
    border-top: 1dp #07080a; border-bottom: 1dp #07080a;
    box-shadow: #ffffff12 0dp 1dp 0dp inset;
  }
  /* Размер — LOGO_WIDTH_DP и LOGO_HEIGHT_DP из logoImage.h, картинка рисуется кодом (D-095) */
  #logo { width: 85dp; height: 26dp; margin-right: 18dp; decorator: image(sint:logo contain); }
  .gap { flex: 1 1 auto; }

  .button {
    height: 24dp; line-height: 22dp; padding: 0 12dp; margin-left: 6dp; box-sizing: border-box;
    border: 1dp #0d0f12; border-radius: 3dp;
    decorator:
      linear-gradient(180deg, #ffffff14 0%, #ffffff04 48%, #ffffff00 52%, #ffffff06 100%) padding-box,
      linear-gradient(180deg, #4a525d 0%, #2c3138 100%) padding-box,
      linear-gradient(180deg, #a3aebd 0%, #4d5561 45%, #121417 100%) border-box;
    box-shadow: #00000090 0dp 1dp 2dp;
    color: #d6dbe3; cursor: pointer;
  }
  .button:hover {
    decorator:
      linear-gradient(180deg, #ffffff1c 0%, #ffffff06 48%, #ffffff00 52%, #ffffff08 100%) padding-box,
      linear-gradient(180deg, #56606c 0%, #343a42 100%) padding-box,
      linear-gradient(180deg, #c0cad6 0%, #5a636f 45%, #15181c 100%) border-box;
    color: #ffffff;
  }
  .button:active {
    decorator:
      linear-gradient(180deg, #00000030 0%, #ffffff00 50%, #ffffff10 100%) padding-box,
      linear-gradient(180deg, #1d4b86 0%, #2b68b3 100%) padding-box,
      linear-gradient(180deg, #0a1a30 0%, #23548f 55%, #7fb2ec 100%) border-box;
    color: #ffffff;
  }
  #preset-button {
    min-width: 200dp; margin-left: 0;
    background-color: #0b0e12;
    decorator: linear-gradient(170deg, #ffffff0a 0%, #ffffff00 60%);
    border: 1dp #000000; border-bottom-color: #58616e; border-right-color: #2c3138;
    box-shadow: #000000d0 0dp 2dp 4dp inset;
    color: #9fc4f5;
  }
  #preset-button:hover { background-color: #11151b; }

  #breadcrumbs { display: flex; flex-direction: row; align-items: center; }
  .crumb { color: #7f8896; cursor: pointer; }
  .crumb:hover { color: #d6dbe3; }
  .crumb.current { color: #d6dbe3; cursor: default; }
  .crumb-separator { color: #4c5361; margin: 0 6dp; }

  #sources { display: flex; flex-direction: row; align-items: center; margin-left: 24dp; }
  .source { position: relative; padding-right: 18dp; color: #f0923a; }

  #canvas {
    position: relative; flex: 1 1 auto; overflow: hidden; clip: always;
    background-color: #0e1116; drag: drag;
    decorator: radial-gradient(circle farthest-corner at 50% 40%, #28405c1c 0%, #00000000 55%, #00000070 100%);
    box-shadow: #000000e0 0dp 3dp 8dp inset;
  }
  #world { position: absolute; left: 0; top: 0; width: 1dp; height: 1dp; }
  #wires { position: absolute; left: 0; top: 0; width: 100%; height: 100%; pointer-events: none; z-index: 5; }
  #draft { position: absolute; left: 0; top: 0; width: 100%; height: 100%; pointer-events: none; z-index: 20; }

  .block {
    position: absolute; width: 252dp; box-sizing: border-box;
    border: 1dp #0b0d10; border-radius: 4dp; drag: drag;
    decorator:
      linear-gradient(115deg, #ffffff00 10%, #ffffff16 26%, #ffffff00 42%, #00000018 56%, #ffffff00 66%, #ffffff0c 80%, #ffffff00 94%) padding-box,
      image(sint:brushed repeat) padding-box,
      linear-gradient(180deg, #2e333b 0%, #23272e 45%, #1b1e23 100%) padding-box,
      linear-gradient(135deg, #d0d9e4 0%, #7d8795 15%, #474f5a 45%, #2a2f36 100%) border-box;
    box-shadow: #000000 0dp 0dp 0dp 1dp, #000000b0 0dp 4dp 10dp;
  }
  /* Рамка закрыта кромкой, поэтому наведение и заглушка видны по обводке тенью */
  .block:hover { box-shadow: #3d8de870 0dp 0dp 0dp 1dp, #000000c0 0dp 5dp 12dp; }
  .block.stub { box-shadow: #c0485c 0dp 0dp 0dp 1dp, #000000b0 0dp 4dp 10dp; }
  .block.small { width: 134dp; }

  .block-header {
    display: flex; flex-direction: row; align-items: center;
    height: 24dp; padding: 0 6dp 0 10dp;
    decorator:
      linear-gradient(100deg, #ffffff00 4%, #ffffff22 20%, #ffffff00 36%, #00000016 50%, #ffffff00 60%, #ffffff12 76%, #ffffff00 92%),
      image(sint:brushed repeat),
      linear-gradient(180deg, #4a515c 0%, #363c45 50%, #2a2e35 100%);
    border-bottom: 1dp #0b0d10; border-radius: 3dp 3dp 0 0;
  }
  .block-title { flex: 1 1 auto; white-space: nowrap; overflow: hidden; color: #e2e6ec; font-effect: shadow(0dp 1dp #000000a0); }
  .block-button {
    width: 16dp; height: 16dp; line-height: 15dp; margin-left: 4dp; text-align: center;
    border-radius: 3dp; color: #8a93a1; cursor: pointer;
  }
  .block-button:hover { background-color: #4a525e; color: #ffffff; }
  .power-dot {
    width: 8dp; height: 8dp; margin: 4dp auto 0; border-radius: 4dp; pointer-events: none;
    decorator: radial-gradient(circle farthest-side at 35% 30%, #eaffde 0%, #8ee07a 35%, #3f9233 100%);
    box-shadow: #000000a0 0dp 0dp 0dp 1dp, #7fd36a50 0dp 0dp 5dp 1dp;
  }
  .disabled .power-dot {
    decorator: radial-gradient(circle farthest-side at 35% 30%, #8a919c 0%, #454b55 45%, #22262c 100%);
    box-shadow: #000000a0 0dp 0dp 0dp 1dp;
  }

  .block-body { display: flex; flex-direction: row; flex-wrap: wrap; padding: 6dp 4dp; min-height: 28dp; }
  .block-note { padding: 4dp 10dp 8dp; color: #7f8896; }
  .open-button { margin: 0 10dp 8dp; text-align: center; }

)RML"
R"RML(
  .knob-cell { position: relative; width: 60dp; height: 72dp; text-align: center; }
  .knob-cell.legal knob { background-color: #f0923a38; border-radius: 19dp; }
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
  .port.audio { decorator: radial-gradient(circle farthest-corner at 35% 30%, #a8cffa 0%, #4f9cf2 45%, #1d5aa6 100%); }
  .port.modulation {
    border-radius: 3dp;
    decorator: radial-gradient(circle farthest-corner at 35% 30%, #ffd2a0 0%, #f39a45 45%, #a85a17 100%);
  }
  .port:hover { decorator: none; background-color: #ffffff; }
  .port.legal { decorator: radial-gradient(circle farthest-corner at 35% 30%, #eaffde 0%, #8ee07a 40%, #3f9233 100%); }
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
    border-top: 1dp #000000;
    decorator:
      linear-gradient(100deg, #ffffff00 0%, #ffffff14 18%, #ffffff00 34%, #00000018 48%, #ffffff00 58%, #ffffff0e 74%, #ffffff00 90%) padding-box,
      image(sint:worn repeat) padding-box,
      linear-gradient(180deg, #2c3138 0%, #1d2025 100%) padding-box,
      linear-gradient(90deg, #a3aebd 0%, #5d6672 35%, #3a414b 100%) border-box;
  }
  #macros, #matrix {
    background-color: #14171b;
    decorator: linear-gradient(180deg, #00000030 0%, #ffffff05 100%), image(sint:scuffed repeat);
    border: 1dp #07080a; border-bottom-color: #4f5864; border-right-color: #353b44; border-radius: 4dp;
    box-shadow: #000000c0 0dp 3dp 7dp inset;
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
    border: 1dp #0a0c0f; border-radius: 3dp;
    decorator:
      linear-gradient(100deg, #ffffff00 0%, #ffffff10 22%, #ffffff00 44%, #00000014 60%, #ffffff00 76%) padding-box,
      image(sint:brushed repeat) padding-box,
      linear-gradient(180deg, #252a31 0%, #1b1f24 100%) padding-box,
      linear-gradient(180deg, #5d6672 0%, #2a2f36 50%, #0d0f12 100%) border-box;
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
    decorator: linear-gradient(180deg, #9ccaff 0%, #4f9cf2 35%, #2a6fc4 100%);
    box-shadow: #3d8de840 0dp 0dp 4dp;
  }
  .route-center { position: absolute; left: 50%; top: 0; width: 1dp; height: 100%; background-color: #4c5361; pointer-events: none; }
  .route-value { flex: 0 0 40dp; text-align: right; color: #c3cad5; }
  .matrix-empty { padding: 14dp 0; color: #4c5361; }

  #popup {
    position: absolute; min-width: 180dp; max-height: 320dp; overflow: hidden auto; padding: 4dp;
    border: 1dp #0a0c0f; border-radius: 4dp; z-index: 30;
    decorator:
      image(sint:brushed repeat) padding-box,
      linear-gradient(180deg, #2e333b 0%, #1f2329 100%) padding-box,
      linear-gradient(135deg, #a3aebd 0%, #4d5561 25%, #15181c 100%) border-box;
    box-shadow: #000000c0 0dp 6dp 16dp;
  }
  .menu-item { height: 24dp; line-height: 24dp; padding: 0 10dp; white-space: nowrap; border-radius: 3dp; cursor: pointer; }
  .menu-item:hover { background-color: #2a5ea8; color: #ffffff; }
  .menu-heading { height: 22dp; line-height: 22dp; padding: 0 10dp; color: #7f8896; }

  #prompt {
    position: absolute; left: 50%; top: 120dp; width: 320dp; margin-left: -160dp; padding: 12dp;
    border: 1dp #0a0c0f; border-radius: 4dp; z-index: 40;
    decorator:
      linear-gradient(115deg, #ffffff00 10%, #ffffff0c 30%, #ffffff00 50%) padding-box,
      image(sint:brushed repeat) padding-box,
      linear-gradient(180deg, #2e333b 0%, #1f2329 100%) padding-box,
      linear-gradient(135deg, #a3aebd 0%, #4d5561 25%, #15181c 100%) border-box;
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
    <div id="logo"></div>
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
