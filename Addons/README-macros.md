Macro window files (ported from the Windows x64 client source, 2026-11-20). They override the stock Interface/AddOns/Blizzard_MacroUI files:

* Blizzard_MacroUI/ : Blizzard_MacroUI.lua, Blizzard_MacroUI.xml, FTA_MacroEditor.lua (the .toc stays the stock one from the MPQ). Local fix: the Find / Replace boxes use a plain backdrop instead of InputBoxTemplate (the template's middle texture did not draw).
* ncBiggerMacros/ncBiggerMacros.lua : the old 1024-character add-on from the base package, changed to step aside when the client has native long macros (GetMacroBodyLimits) and to migrate its "/click ExtraMacrosN" stubs. Original sha256 c5ce9537...; its original is in FTA-Project/archive/ncBiggerMacros-original/.

Both have to reach players through the launcher (addon fixes bundle); until then they only exist as loose files in the developer's client.
