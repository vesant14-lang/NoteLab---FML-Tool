#pragma once

void emitMedia(Emit& e, const BlockNode& n, int id) {
    const bool luaCode = luaLang(e);
    const bool video = n.key == "do.video", screamer = n.key == "do.screamer";
    const auto mediaKind = video ? ResourceKind::Video : ResourceKind::Image;
    const std::string path = n.args.empty() ? std::string() : resourceNativeKey(n.args[0].value, mediaKind);
    if (path.empty()) {
        conflict(*e.code, Severity::Error, n.key, id, "Choose a valid mod media resource.", "Elige un recurso multimedia válido del mod.");
        return;
    }
    const std::string soundPath = screamer && n.args.size() > 1 ? resourceNativeKey(n.args[1].value, ResourceKind::Sound) : std::string();
    if (screamer && soundPath.empty()) {
        conflict(*e.code, Severity::Error, n.key, id, "A screamer needs both its image and sound.", "Un screamer necesita imagen y sonido.");
        return;
    }
    const std::string suffix = std::to_string(id), tag = e.tag + "_media_" + suffix;
    const std::string seconds = value(e, n, screamer ? 2 : 1);
    const std::string volume = screamer ? value(e, n, 3) : video ? value(e, n, 2) : "1";
    const std::string opacity = video || screamer ? "1" : value(e, n, 2);
    const std::string cooldown = value(e, n, 4);
    const std::string fit = n.args.size() > (screamer ? 5u : 3u) ? n.args[screamer ? 5 : 3].value : "contain";
    const std::string last = "notelabMediaLast" + suffix, sprite = "notelabMediaSprite" + suffix, timer = "notelabMediaTimer" + suffix, sound = "notelabMediaSound" + suffix;
    const std::string now = luaCode ? "(getSongPosition() / 1000)" : "(FlxG.game.ticks / 1000.0)";
    if (luaCode) {
        (*e.globals)[id] = "local " + last + " = -1000000\n";
        line(e, "if " + now + " < " + last + " or " + now + " - " + last + " >= " + cooldown + " then");
        ++e.depth;
        line(e, last + " = " + now);
        if (!video) {
            if (screamer) line(e, "stopSound(" + lua(tag + "_sound") + ")");
            line(e, "cancelTimer(" + lua(e.tag + "_" + suffix) + ")");
            line(e, "removeLuaSprite(" + lua(tag) + ", true)");
            line(e, "makeLuaSprite(" + lua(tag) + ", " + lua(path) + ", 0, 0)");
            line(e, "setObjectCamera(" + lua(tag) + ", 'other')");
            line(e, "setProperty(" + lua(tag + ".alpha") + ", " + opacity + ")");
            if (fit == "stretch") line(e, "setGraphicSize(" + lua(tag) + ", screenWidth, screenHeight)");
            else {
                const std::string scale = "math." + std::string(fit == "cover" ? "max" : "min") + "(screenWidth / math.max(1, getProperty(" + lua(tag + ".width") + ")), screenHeight / math.max(1, getProperty(" + lua(tag + ".height") + ")))";
                line(e, "scaleObject(" + lua(tag) + ", " + scale + ", " + scale + ")");
            }
            line(e, "updateHitbox(" + lua(tag) + ")");
            line(e, "screenCenter(" + lua(tag) + ")");
            line(e, "addLuaSprite(" + lua(tag) + ", true)");
            if (screamer) line(e, "playSound(" + lua(soundPath) + ", " + volume + ", " + lua(tag + "_sound") + ")");
            line(e, "runTimer(" + lua(e.tag + "_" + suffix) + ", " + seconds + ")");
            (*e.timers)[id] = "\t\tremoveLuaSprite(" + lua(tag) + ", true)\n" + (screamer ? "\t\tstopSound(" + lua(tag + "_sound") + ")\n" : "");
        } else {
            std::string body = "var hostState = game; var old = hostState.variables.get(" + hx(tag) + "); if (old != null) { hostState.remove(old); old.destroy(); }\n";
            body += "var cls = Type.resolveClass(\"hxvlc.flixel.FlxVideoSprite\"); if (cls == null) cls = Type.resolveClass(\"hxcodec.flixel.FlxVideoSprite\");\n";
            body += "if (cls != null) { var clip = Type.createInstance(cls, [0, 0]); hostState.variables.set(" + hx(tag) + ", clip); clip.cameras = [hostState.camOther]; clip.scrollFactor.set(); hostState.add(clip);\n";
            body += "var close = function() { if (hostState.variables.get(" + hx(tag) + ") == clip) { hostState.variables.remove(" + hx(tag) + "); hostState.remove(clip); if (clip.exists) clip.destroy(); } };\n";
            body += "clip.bitmap.onEndReached.add(close); clip.bitmap.onFormatSetup.add(function() { ";
            body += fit == "stretch" ? "clip.setGraphicSize(FlxG.width, FlxG.height);" : "var scale = Math." + std::string(fit == "cover" ? "max" : "min") + "(FlxG.width / Math.max(1, clip.width), FlxG.height / Math.max(1, clip.height)); clip.scale.set(scale, scale);";
            body += "clip.updateHitbox(); clip.screenCenter(); }); if (clip.load(Paths.video(" + hx(path) + "))) { clip.autoVolumeHandle = false; clip.bitmap.volume = Std.int(" + volume + " * 100); clip.play(); new flixel.util.FlxTimer().start(" + seconds + ", function(_) { close(); }); } else close(); }\n";
            line(e, "if type(runHaxeCode) == 'function' then");
            ++e.depth; line(e, "runHaxeCode(" + lua(body) + ")"); --e.depth;
            line(e, "else debugPrint('Note Lab: video overlay requires Haxe and video support') end");
        }
        --e.depth; line(e, "end");
        return;
    }
    const bool vslice = e.engine == Engine::VSlice;
    if (vslice) {
        use(e, "flixel.FlxG"); use(e, "flixel.FlxSprite"); use(e, "flixel.util.FlxTimer");
        use(e, "funkin.play.PlayState"); use(e, "funkin.Paths");
        if (screamer) use(e, "funkin.audio.FunkinSound");
        if (video) use(e, "funkin.graphics.video.FunkinVideoSprite");
    }
    const std::string type = video && vslice ? "FunkinVideoSprite" : "FlxSprite";
    (*e.globals)[id] = std::string(vslice ? "  " : "") + "var " + sprite + (vslice ? ":" + type : "") + " = null;\n" +
        std::string(vslice ? "  " : "") + "var " + timer + (vslice ? ":FlxTimer" : "") + " = null;\n" +
        std::string(vslice ? "  " : "") + "var " + last + " = -1000000.0;\n";
    if (screamer) (*e.globals)[id] += std::string(vslice ? "  " : "") + "var " + sound + (vslice ? ":FunkinSound" : "") + " = null;\n";
    line(e, "if (" + now + " < " + last + " || " + now + " - " + last + " >= " + cooldown + ") {"); ++e.depth;
    line(e, last + " = " + now + ";");
    const std::string host = vslice ? "PlayState.instance." : "";
    const std::string hud = vslice ? "PlayState.instance.camHUD" : "camHUD";
    line(e, "if (" + timer + " != null) " + timer + ".cancel();");
    if (screamer) line(e, "if (" + sound + " != null) { if (" + sound + ".exists) { " + sound + ".stop(); " + sound + ".destroy(); } " + sound + " = null; }");
    line(e, "if (" + sprite + " != null) { " + host + "remove(" + sprite + "); " + sprite + ".destroy(); " + sprite + " = null; }");
    line(e, "var hostState = " + std::string(vslice ? "PlayState.instance" : "FlxG.state") + ";");
    line(e, "var clip = new " + std::string(video ? vslice ? "FunkinVideoSprite" : "FlxVideoSprite" : "FlxSprite") + "(0, 0);");
    line(e, sprite + " = clip;");
    if (!video) line(e, "clip.loadGraphic(Paths.image(" + hx(path) + "));");
    line(e, "clip.cameras = [" + hud + "]; clip.scrollFactor.set(); clip.alpha = " + opacity + ";");
    line(e, host + "add(clip);");
    line(e, "var close = function() { if (" + sprite + " == clip) { " + sprite + " = null; hostState.remove(clip); if (clip.exists) clip.destroy();" + (screamer ? " if (" + sound + " != null) { if (" + sound + ".exists) { " + sound + ".stop(); " + sound + ".destroy(); } " + sound + " = null; }" : "") + " } };");
    if (video) {
        line(e, "clip.bitmap.onEndReached.add(close);");
        line(e, "clip.bitmap.onFormatSetup.add(function() {"); ++e.depth;
    }
    if (fit == "stretch") line(e, "clip.setGraphicSize(FlxG.width, FlxG.height);");
    else {
        line(e, "var scale = Math." + std::string(fit == "cover" ? "max" : "min") + "(FlxG.width / Math.max(1, clip.width), FlxG.height / Math.max(1, clip.height));");
        line(e, "clip.scale.set(scale, scale);");
    }
    line(e, "clip.updateHitbox(); clip.screenCenter();");
    if (video) { --e.depth; line(e, "});"); line(e, "if (clip.load(Paths.video(" + hx(path) + "))) {"); ++e.depth;
        line(e, "clip.autoVolumeHandle = false; clip.bitmap.volume = Std.int(" + volume + " * 100); clip.play();"); }
    if (screamer) {
        if (vslice) line(e, sound + " = FunkinSound.load(Paths.sound(" + hx(soundPath) + "), " + volume + ", false, false, true);");
        else {
            line(e, sound + " = new FlxSound();");
            line(e, sound + ".loadEmbedded(Paths.sound(" + hx(soundPath) + "), false, false);");
            line(e, sound + ".volume = " + volume + "; FlxG.sound.list.add(" + sound + "); " + sound + ".play();");
        }
    }
    line(e, timer + " = new FlxTimer().start(" + seconds + ", function(_) { close(); });");
    if (video) { --e.depth; line(e, "} else close();"); }
    --e.depth; line(e, "}");
}
