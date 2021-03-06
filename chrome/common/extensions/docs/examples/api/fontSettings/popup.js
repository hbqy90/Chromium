// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var scriptList;

// Mapping between font list ids and the generic family setting they
// represent.
var genericFamilies = [
  { fontList: 'standardFontList', name: 'standard' },
  { fontList: 'serifFontList', name: 'serif' },
  { fontList: 'sansSerifFontList', name: 'sansserif' },
  { fontList: 'fixedFontList', name: 'fixed' }
];

var DEFAULT_SCRIPT = 'Qaaa';

function getSelectedScript() {
  return scriptList.options[scriptList.selectedIndex].value;
}

function getSelectedFont(fontList) {
  return fontList.options[fontList.selectedIndex].value;
}

// Populates the font lists with the list of system fonts from |fonts|.
function populateLists(fonts) {
  for (var i = 0; i < genericFamilies.length; i++) {
    var list = document.getElementById(genericFamilies[i].fontList);

    // Add special "(none)" item to indicate fallback to the non-per-script
    // font setting. The Font Settings API uses the empty string to indicate
    // fallback.
    var noneItem = document.createElement('option');
    noneItem.value = '';
    noneItem.text = '(none)';
    list.add(noneItem);

    for (var j = 0; j < fonts.length; j++) {
      var item = document.createElement('option');
      item.value = fonts[j].fontName;
      item.text = fonts[j].localizedName;
      list.add(item);
    }
  }

  updateFontListsForScript();
}

// Returns a function that updates the font setting for |genericFamily|
// to match the selected value in |fontList|. It can be used as an event
// handler for selection changes in |fontList|.
function getFontChangeHandler(fontList, genericFamily) {
  return function() {
    var script = getSelectedScript();
    var font = getSelectedFont(fontList);

    var details = {};
    details.genericFamily = genericFamily;
    details.fontName = font;
    if (script != DEFAULT_SCRIPT)
      details.script = script;

    chrome.experimental.fontSettings.setFont(details);
  };
}

// Sets the selected value of |fontList| to |fontName|.
function setSelectedFont(fontList, fontName) {
  var script = getSelectedScript();
  var i;
  for (i = 0; i < fontList.length; i++) {
    if (fontName == fontList.options[i].value) {
      fontList.selectedIndex = i;
      break;
    }
  }
  if (i == fontList.length) {
    console.warn("font '" + fontName + "' for " + fontList.id + ' for ' +
        script + ' is not on the system');
  }
}

// Returns a callback function that sets the selected value of |list| to the
// font returned from |chrome.experimental.fontSettings.getFont|.
function getFontHandler(list) {
  return function(details) {
    setSelectedFont(list, details.fontName);
    list.disabled = !isControllableLevel(details.levelOfControl);
  };
}

// Called when the script list selection changes. Sets the selected value of
// each font list to the current font setting, and updates the document's lang
// so that the samples are shown in the current font setting.
function updateFontListsForScript() {
  var script = getSelectedScript();

  for (var i = 0; i < genericFamilies.length; i++) {
    var list = document.getElementById(genericFamilies[i].fontList);
    var family = genericFamilies[i].name;

    var details = {};
    details.genericFamily = family;
    if (script != DEFAULT_SCRIPT) {
      details.script = script;
      // For font selection it's the script code that matters, not language, so
      // just use en for lang.
      document.body.lang = 'en-' + script;
    } else {
      document.body.lang = '';
    }

    chrome.experimental.fontSettings.getFont(details, getFontHandler(list));
  }
}

// Returns a function to be called when the user changes the font size
// input element |elem|. The function calls the Font Settings Extension API
// function |setter| to commit the change.
function getFontSizeChangedFunc(elem, setter) {
  return function() {
    var pixelSize = parseInt(elem.value);
    if (!isNaN(pixelSize)) {
      setter({ pixelSize: pixelSize });
    }
  }
}

function isControllableLevel(levelOfControl) {
  return levelOfControl == 'controllable_by_this_extension' ||
      levelOfControl == 'controlled_by_this_extension';
}

// Returns a function to be used as a listener for font size setting changed
// events from the Font Settings Extension API. The function updates the
// input element |elem| to reflect the change.
function getFontSizeChangedOnBrowserFunc(elem) {
  return function(details) {
    elem.value = details.pixelSize.toString();
    elem.disabled = !isControllableLevel(details.levelOfControl);
  }
}

// Maps the text input HTML element with |id| to the extension API accessor
// functions |getter| and |setter| for a setting and onChange event |apiEvent|
// for the setting.
function initFontSizePref(id, getter, setter, apiEvent) {
  var elem = document.getElementById(id);
  getter({}, function(details) {
    elem.value = details.pixelSize.toString();
    elem.disabled = !isControllableLevel(details.levelOfControl);
  });
  elem.addEventListener('change', getFontSizeChangedFunc(elem, setter));
  apiEvent.addListener(getFontSizeChangedOnBrowserFunc(elem));
}

// Updates the encoding list to reflect the browser encoding setting.
function updateEncoding() {
  chrome.experimental.fontSettings.getDefaultCharacterSet({},
                                                          function(details) {
    var list = document.getElementById('encodingList');
    var i;
    for (i = 0; i < list.length; i++) {
      if (details.charset == list.options[i].value) {
        list.selectedIndex = i;
        break;
      }
    }
    if (i == list.length) {
      console.warn("encoding '" + details.charset + "' not found in list.");
    }
    list.disabled = !isControllableLevel(details.levelOfControl);
  });
}

// Sets browser encoding setting to currently selected value of the encoding
// list.
function setEncoding() {
  var list = document.getElementById('encodingList');
  chrome.experimental.fontSettings.setDefaultCharacterSet({
    charset: list.options[list.selectedIndex].value
  });
};

// Adds event handlers for encoding and initializes the selected value of
// the encoding list.
function initEncoding() {
  var list = document.getElementById('encodingList');

  list.addEventListener('change', setEncoding);
  chrome.experimental.fontSettings.onDefaultCharacterSetChanged.addListener(
      updateEncoding);

  updateEncoding();
}

function clearAllSettings() {
  var scripts =
      ["Arab", "Armn", "Beng", "Cans", "Cher", "Cyrl", "Deva", "Ethi", "Geor",
       "Grek", "Gujr", "Guru", "Hang", "Hans", "Hant", "Hebr", "Hrkt", "Knda",
       "Khmr", "Laoo", "Mlym", "Mong", "Mymr", "Orya", "Sinh", "Taml", "Telu",
       "Thaa", "Thai", "Tibt", "Yiii", "Zyyy"];
  var families =
      ["standard", "sansserif", "serif", "fixed", "cursive", "fantasy"];
  for (var i = 0; i < scripts.length; i++) {
    for (var j = 0; j < families.length; j++) {
      chrome.experimental.fontSettings.clearFont({
        script: scripts[i],
        genericFamily: families[j]
      });
    }
  }

  chrome.experimental.fontSettings.clearDefaultCharacterSet();
  chrome.experimental.fontSettings.clearDefaultFixedFontSize();
  chrome.experimental.fontSettings.clearDefaultFontSize();
  chrome.experimental.fontSettings.clearMinimumFontSize();
}

function init() {
  scriptList = document.getElementById('scriptList');
  scriptList.addEventListener('change',
                              updateFontListsForScript);

  // Populate the font lists.
  chrome.experimental.fontSettings.getFontList(populateLists);

  // Add change handlers to the font lists.
  for (var i = 0; i < genericFamilies.length; i++) {
    var list = document.getElementById(genericFamilies[i].fontList);
    var handler = getFontChangeHandler(list, genericFamilies[i].name);
    list.addEventListener('change', handler);
  }

  chrome.experimental.fontSettings.onFontChanged.addListener(
      updateFontListsForScript);

  initFontSizePref('defaultFontSize',
                   chrome.experimental.fontSettings.getDefaultFontSize,
                   chrome.experimental.fontSettings.setDefaultFontSize,
                   chrome.experimental.fontSettings.onDefaultFontSizeChanged);
  initFontSizePref(
      'defaultFixedFontSize',
      chrome.experimental.fontSettings.getDefaultFixedFontSize,
      chrome.experimental.fontSettings.setDefaultFixedFontSize,
      chrome.experimental.fontSettings.onDefaultFixedFontSizeChanged);
  initFontSizePref('minFontSize',
                   chrome.experimental.fontSettings.getMinimumFontSize,
                   chrome.experimental.fontSettings.setMinimumFontSize,
                   chrome.experimental.fontSettings.onMinimumFontSizeChanged);
  initEncoding();

  var clearButton = document.getElementById('clearButton');
  clearButton.addEventListener('click', clearAllSettings);
}

document.addEventListener('DOMContentLoaded', init);
