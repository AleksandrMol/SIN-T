#include "./editorDocument.h"

// Длинная строка разбита на части: у MSVC ограничена длина одного литерала
const char* const EDITOR_DOCUMENT = R"RML(
<rml>
<head>
<title>SIN-T</title>
<style>
  body, div, wire-layer, knob, value-field, wave-screen { display: block; }
  input { display: inline-block; }

  /*
    Шрифт Jura (D-101): тонкий, поэтому основной текст Medium, а мелкие подписи SemiBold.
    Буквы у него широкие: названия ручек 9dp, иначе «Bend range» не помещается в ячейку ручки
  */
  body {
    width: 100%; height: 100%;
    display: flex; flex-direction: column;
    background-color: #07080a; color: #d6dbe3;
    font-family: "Jura"; font-size: 12dp; font-weight: 500;
    padding: 4dp;
  }

  /* Полоса прокрутки: жёлоб тёмного стекла и металлический ползунок с насечкой, отступ не даёт ей лечь на фаску панели */
  scrollbarvertical { width: 12dp; margin: 6dp 4dp 6dp 0; }
  scrollbarvertical slidertrack { decorator: plate(display 2dp none); }
  scrollbarvertical sliderbar { min-height: 28dp; cursor: pointer; decorator: plate(button 2dp grip); }
  scrollbarvertical sliderbar:hover, scrollbarvertical sliderbar:active { decorator: plate(button-hover 2dp grip); }
  scrollbarhorizontal { height: 0; }

  /*
    Пластины металла рисует свой декоратор plate(вид срез детали) (plateDecorator.h): срезанные углы,
    фаска, текстура и винты. Тёмный фон окна между пластинами — швы, как на референсе D-093.
    Свет падает сверху слева
  */
  #toolbar { display: flex; flex-direction: row; flex: 0 0 44dp; }
  .toolbar-plate {
    display: flex; flex-direction: row; align-items: center;
    margin-right: 4dp; padding: 0 10dp;
    decorator: plate(frame 8dp none);
  }
  #logo-plate { padding: 0 16dp; decorator: plate(frame 8dp screws); }
  #toolbar-filler { flex: 1 1 auto; margin-right: 0; }

  #nav { display: flex; flex-direction: row; align-items: center; flex: 0 0 32dp; margin-top: 4dp; }
  #nav-plate {
    display: flex; flex-direction: row; align-items: center; flex: 1 1 auto; height: 100%;
    margin-right: 4dp; padding: 0 14dp;
    decorator: plate(panel 8dp none);
  }
  #nav > .button { height: 100%; line-height: 30dp; margin-left: 0; padding: 0 14dp; }

  /* Размер — LOGO_WIDTH_DP и LOGO_HEIGHT_DP из logoImage.h, картинка рисуется кодом (D-095) */
  #logo { width: 85dp; height: 26dp; decorator: image(sint:logo contain); }
  .gap { flex: 1 1 auto; }

  .button {
    height: 26dp; line-height: 26dp; padding: 0 12dp; margin-left: 6dp; box-sizing: border-box;
    decorator: plate(button 3dp none);
    color: #d6dbe3; cursor: pointer; font-effect: shadow(0dp 1dp #000000a0);
  }
  .button:hover { decorator: plate(button-hover 3dp none); color: #ffffff; }
  .button:active { decorator: plate(button-active 3dp none); color: #ffffff; }
  .toolbar-plate .button:first-child { margin-left: 0; }
  #preset-button {
    min-width: 200dp; margin-left: 0; padding: 0 10dp;
    decorator: plate(display 3dp none);
    color: #9fc4f5; font-effect: none;
  }
  #preset-button:hover { color: #c4dcfa; }

  #breadcrumbs { display: flex; flex-direction: row; align-items: center; }
  .crumb { color: #7f8896; cursor: pointer; }
  .crumb:hover { color: #d6dbe3; }
  .crumb.current { color: #d6dbe3; cursor: default; }
  .crumb-separator { color: #4c5361; margin: 0 6dp; }

  #sources { display: flex; flex-direction: row; align-items: center; margin-left: 24dp; }
  .source { position: relative; padding-right: 18dp; color: #f0923a; }

  /*
    Базовый размер 0 у рамы и холста: высоту даёт flex-grow, а с auto RmlUi раскладывал бы
    все блоки холста ради измерения на каждом уровне вложенности.
    Холст прямоугольный, поэтому срез рамы не больше отступа·(2 − √2): иначе в углу рама
    становится тоньше, чем по сторонам, и угол холста с косынкой выглядывает из среза
  */
  #screen {
    display: flex; flex-direction: column; flex: 1 1 0dp; margin-top: 4dp; padding: 7dp;
    decorator: plate(frame 4dp none);
  }
  #canvas {
    position: relative; flex: 1 1 0dp; overflow: hidden; clip: always;
    background-color: #0e1116; drag: drag;
    box-shadow: #000000 0dp 0dp 0dp 1dp;
  }
  /*
    Сетка едет вместе со схемой, как background-position: слой больше холста на период рисунка
    (отступ справа и снизу — GRID_PERIOD_DP из gridDecorator.h), редактор сдвигает его на остаток от прокрутки.
    Тень, виньетка и косынки лежат над сеткой: экран утоплен в раму, а линии уходят под её край
  */
  #grid {
    position: absolute; left: 0; top: 0; width: 100%; height: 100%; padding: 0 72dp 72dp 0;
    pointer-events: none; decorator: grid();
  }
  #canvas-shade {
    position: absolute; left: 0; top: 0; width: 100%; height: 100%; pointer-events: none;
    decorator:
      plate(bare 0dp gussets),
      radial-gradient(circle farthest-corner at 50% 40%, #28405c1c 0%, #00000000 55%, #00000070 100%);
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

  /* Высоту блока знает авторасстановка (getBlockHeight в graphLayout): правка размеров здесь требует правки и там, тест сверяет их */
  .block-body { display: flex; flex-direction: row; flex-wrap: wrap; padding: 6dp 4dp; }
  .block-note { padding: 4dp 10dp 8dp; line-height: 16dp; color: #7f8896; }
  .open-button { margin: 0 10dp 8dp; text-align: center; }

)RML"
R"RML(
  .knob-cell { position: relative; width: 60dp; height: 72dp; text-align: center; }
  .knob-cell.legal knob { background-color: #f0923a38; border-radius: 19dp; }
  knob { width: 38dp; height: 38dp; margin: 0 auto; cursor: pointer; drag: drag; }
  .knob-name { color: #c3cad5; font-size: 9dp; font-weight: 600; white-space: nowrap; overflow: hidden; }
  .knob-value { color: #7f8896; font-size: 10dp; font-weight: 600; white-space: nowrap; overflow: hidden; }
  .knob-mark {
    position: absolute; right: 8dp; top: 0; width: 6dp; height: 6dp;
    border-radius: 3dp; background-color: #f0923a;
  }
  .knob-mark.unexported { left: 8dp; right: auto; background-color: #e0566c; }

  /*
    Лицевая сторона осциллятора (D-108): волна переключается стрелками, на экране поля высоты тона сверху
    и фазы снизу, под экраном ряд ручек. Поле работает как ручка: подпись и число не ловят мышь,
    поэтому щелчок, колесо и провод модуляции попадают в само поле
  */
  .osc-face { flex-direction: column; flex-wrap: nowrap; padding: 6dp 6dp 2dp; }
  .wave-select { display: flex; flex-direction: row; align-items: center; height: 20dp; margin-bottom: 4dp; }
  .step-button {
    flex: 0 0 20dp; height: 20dp; line-height: 18dp; text-align: center; font-size: 14dp;
    decorator: plate(button 2dp none); color: #aeb8c6; cursor: pointer;
  }
  .step-button:hover { decorator: plate(button-hover 2dp none); color: #ffffff; }
  .step-button:active { decorator: plate(button-active 2dp none); color: #ffffff; }
  .osc-screen { padding: 3dp; decorator: plate(display 4dp none); }
  .field-row { display: flex; flex-direction: row; }
  .field {
    position: relative; display: flex; flex-direction: row; align-items: center; flex: 1 1 0dp;
    height: 18dp; padding: 0 5dp; margin-right: 3dp; box-sizing: border-box;
    background-color: #ffffff0a; border-radius: 2dp; cursor: pointer; drag: drag;
  }
  .field:last-child { margin-right: 0; }
  .field:hover { background-color: #ffffff16; }
  .field.legal { background-color: #f0923a38; }
  /* Пластина рисуется поверх фона, поэтому наведение и подсветка цели провода у Wave — полупрозрачный слой над ней */
  .field.wave-name { height: 20dp; margin: 0 4dp; background-color: #00000000; decorator: plate(display 3dp none); }
  .field.wave-name:hover { decorator: linear-gradient(180deg, #ffffff10 0%, #ffffff10 100%), plate(display 3dp none); }
  .field.wave-name.legal { decorator: linear-gradient(180deg, #f0923a38 0%, #f0923a38 100%), plate(display 3dp none); }
  /* Четыре поля высоты тона в ряд: отступы меньше, ширины по самому длинному значению «-4», «-48», «-99.9», «-47.99» */
  .pitch-octave, .pitch-semi, .pitch-fine, .pitch-coarse { padding: 0 4dp; }
  .pitch-octave .field-name, .pitch-semi .field-name, .pitch-fine .field-name, .pitch-coarse .field-name { margin-right: 3dp; }
  .pitch-octave { flex-grow: 43; }
  .pitch-semi { flex-grow: 53; }
  .pitch-fine { flex-grow: 60; }
  .pitch-coarse { flex-grow: 67; }
  .field-icon { flex: 0 0 auto; margin-right: 4dp; color: #7f8896; font-size: 13dp; pointer-events: none; }
  .field-name { flex: 0 0 auto; margin-right: 4dp; color: #7f8896; font-size: 9dp; font-weight: 600; white-space: nowrap; pointer-events: none; }
  .field-value { flex: 1 1 auto; text-align: right; color: #9fc4f5; font-size: 11dp; white-space: nowrap; overflow: hidden; pointer-events: none; }
  .wave-name .field-value { text-align: center; }
  .field .knob-mark { right: 2dp; top: 2dp; width: 4dp; height: 4dp; border-radius: 2dp; }
  .field .knob-mark.unexported { left: 2dp; right: auto; }
  .wave-screen { height: 60dp; margin: 3dp 0; }
  .knob-row { display: flex; flex-direction: row; justify-content: center; margin-top: 4dp; }
  .osc-face .knob-cell { width: 46dp; }
  .knob-divider { width: 0; margin: 4dp 3dp 8dp; border-left: 1dp #0b0d10; border-right: 1dp #ffffff14; }

  /*
    Порт — гнездо в металле (D-100): оправа с фаской, свет сверху слева, внутри цветной сердечник.
    Форма различает виды без цвета: аудио круглое, модуляция квадратная.
    Гнездо лежит над слоем проводов: провод уходит под оправу, как штекер
  */
  .port {
    position: absolute; width: 14dp; height: 14dp; box-sizing: border-box; border: 2dp #2a2f36; border-radius: 7dp;
    box-shadow: #000000 0dp 0dp 0dp 1dp, #00000090 0dp 2dp 3dp 1dp;
    drag: drag-drop; cursor: pointer; z-index: 6;
  }
  .port.audio {
    decorator:
      radial-gradient(circle farthest-corner at 35% 30%, #a8cffa 0%, #4f9cf2 45%, #1d5aa6 100%) padding-box,
      linear-gradient(135deg, #d0d9e4 0%, #7d8795 40%, #2a2f36 100%) border-box;
  }
  .port.modulation {
    border-radius: 3dp;
    decorator:
      radial-gradient(circle farthest-corner at 35% 30%, #ffd2a0 0%, #f39a45 45%, #a85a17 100%) padding-box,
      linear-gradient(135deg, #d0d9e4 0%, #7d8795 40%, #2a2f36 100%) border-box;
  }
  .port:hover {
    decorator:
      radial-gradient(circle farthest-corner at 35% 30%, #ffffff 0%, #e8eef6 50%, #a9b4c2 100%) padding-box,
      linear-gradient(135deg, #d0d9e4 0%, #7d8795 40%, #2a2f36 100%) border-box;
  }
  .port.legal {
    decorator:
      radial-gradient(circle farthest-corner at 35% 30%, #eaffde 0%, #8ee07a 40%, #3f9233 100%) padding-box,
      linear-gradient(135deg, #d0d9e4 0%, #7d8795 40%, #2a2f36 100%) border-box;
  }
  .port.input { left: -8dp; }
  .port.output { right: -8dp; }
  .port-label { position: absolute; right: 12dp; color: #f0923a; font-size: 10dp; font-weight: 600; pointer-events: none; }
  .source .port { right: 0; top: 3dp; }

  #wire-remove {
    position: absolute; width: 20dp; height: 20dp; line-height: 19dp; text-align: center;
    background-color: #e0566c; color: #0f1216; border-radius: 10dp; cursor: pointer; z-index: 10;
  }

  /* Рама с винтами в углах, внутри утопленная пластина: её срезанные углы оставляют место винтам рамы */
  #bottom { display: flex; flex-direction: row; flex: 0 0 196dp; margin-top: 4dp; }
  #macros, #matrix { display: flex; flex-direction: column; padding: 10dp; decorator: plate(frame 8dp screws); }
  #macros { flex: 0 0 282dp; margin-right: 4dp; }
  #macros-well { flex: 1 1 auto; padding: 4dp 9dp; decorator: plate(sunken 12dp none); }
  .panel-title { color: #aeb8c6; height: 20dp; line-height: 20dp; font-effect: shadow(0dp 1dp #000000c0); }
  #matrix-header .panel-title { color: #6fa8ec; font-size: 14dp; }
  #macro-cells { display: flex; flex-direction: row; flex-wrap: wrap; }
  #macro-cells .knob-cell { width: 61dp; height: 74dp; }
  #macro-cells .port { right: 2dp; top: 22dp; }

  #matrix { flex: 1 1 auto; }
  #matrix-header {
    display: flex; flex-direction: row; align-items: center; flex: 0 0 32dp; margin-bottom: 4dp; padding: 0 12dp 0 16dp;
    decorator: plate(panel 12dp none);
  }
  #matrix-header .button { height: 24dp; line-height: 24dp; }
  #matrix-rows { flex: 1 1 auto; overflow: hidden auto; padding: 6dp 12dp; decorator: plate(sunken 12dp none); }
  /*
    Строка — утопленные ячейки тёмного стекла на дне панели, как на референсе D-093.
    Ширины не зависят от текста, иначе в узком окне столбцы в строках разъезжаются:
    источник и ползунок сужаются вместе с панелью, остальное место у цели
  */
  .route { display: flex; flex-direction: row; align-items: center; height: 28dp; margin-bottom: 4dp; }
  .route-cell { height: 100%; box-sizing: border-box; padding: 0 10dp; decorator: plate(display 3dp none); }
  .route-source, .route-target { display: flex; flex-direction: row; align-items: center; }
  .route-name { flex: 1 1 0dp; white-space: nowrap; overflow: hidden; }
  .route-source { flex: 0 0 auto; width: 26%; min-width: 100dp; max-width: 160dp; color: #f0923a; }
  .route-arrow { position: relative; top: -1dp; flex: 0 0 auto; margin-left: 6dp; font-size: 16dp; color: #66768a; }
  .route-target { flex: 1 1 0dp; margin-left: 4dp; }
  .route-depth {
    position: relative; flex: 0 0 auto; width: 40%; max-width: 160dp; height: 10dp; margin-left: 10dp;
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
  .route-value { flex: 0 0 58dp; margin-left: 4dp; line-height: 28dp; text-align: right; color: #d6dbe3; }
  .route > .block-button { margin-left: 6dp; }
  .matrix-empty { padding: 14dp 0; color: #4c5361; }

  #popup {
    position: absolute; min-width: 180dp; max-height: 320dp; overflow: hidden auto; padding: 4dp;
    z-index: 30;
    decorator: plate(panel 6dp none);
    box-shadow: #000000c0 0dp 6dp 16dp;
  }
  .menu-item { height: 24dp; line-height: 24dp; padding: 0 10dp; white-space: nowrap; border-radius: 3dp; cursor: pointer; }
  .menu-item:hover { background-color: #2a5ea8; color: #ffffff; }
  .menu-heading { height: 22dp; line-height: 22dp; padding: 0 10dp; color: #7f8896; }

  #prompt {
    position: absolute; left: 50%; top: 120dp; width: 340dp; margin-left: -170dp; padding: 16dp 26dp;
    z-index: 40;
    decorator: plate(panel 10dp screws);
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
    <div id="logo-plate" class="toolbar-plate"><div id="logo"></div></div>
    <div class="toolbar-plate"><div id="preset-button" class="button" data-action="presets"></div></div>
    <div class="toolbar-plate">
      <div class="button" data-action="save">Save</div>
      <div class="button" data-action="save-as">Save as</div>
    </div>
    <div id="toolbar-filler" class="toolbar-plate"></div>
  </div>
  <div id="nav">
    <div id="nav-plate">
      <div id="breadcrumbs"></div>
      <div id="sources"></div>
    </div>
    <div class="button" data-action="add-module">+ Add module</div>
  </div>
  <div id="screen">
    <div id="canvas" data-drag="canvas">
      <div id="grid"></div>
      <div id="canvas-shade"></div>
      <div id="world"></div>
      <wire-layer id="wires"></wire-layer>
      <div id="wire-remove" class="hidden" data-action="remove-wire">×</div>
    </div>
  </div>
  <div id="bottom">
    <div id="macros">
      <div id="macros-well">
        <div class="panel-title">Macros</div>
        <div id="macro-cells"></div>
      </div>
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
