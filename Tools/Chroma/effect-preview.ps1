# Plays the superweapon effects on the hardware so they can be judged without
# waiting for a real launch, and tuned without waiting for a build. The maths is
# compiled C# that mirrors ChromaKeyboard.cpp's chromaEffectColor, stepped thirty
# times a second. Whatever rate the game's logic runs at, chromaFlicker steps its noise
# on retail 30 Hz frames (LOGICFRAMES_PER_RETAIL_FRAME), so what this shows is what
# the game shows. The first version was plain PowerShell and managed about eight
# frames a second, which made every effect look like it stuttered.
#
# The constants below are a second copy of the ones in ChromaKeyboard.cpp and
# nothing keeps them together. Tune here, watch, then carry the numbers across by
# hand and rebuild; that is the loop this was written for. A number that differs
# between the two files means this is lying to you.
#
# Close the game first, or it fights this for the keyboard.
#
#   .\effect-preview.ps1                                # three rounds of all three
#   .\effect-preview.ps1 -Rounds 1 -Effects nuke        # one effect, once

param(
    [int] $Rounds = 3,
    [string[]] $Effects = @('nuke', 'laser', 'scud')
)

$ErrorActionPreference = 'Stop'

$source = @'
using System;
using System.Net.Http;
using System.Text;
using System.Threading;

public static class ChromaPreview
{
    const int LOGIC_FPS = 30;

    // The nuke: a thin white shock out in front, and behind it a long body in
    // three bands, red then white then orange, front to back.
    const double NUKE_SHOCK_SPEED = 3.5;
    const double NUKE_SHOCK_LENGTH = 0.40;
    const double NUKE_BODY_SPEED = 1.3;
    const double NUKE_RED_LENGTH = 0.9;
    const double NUKE_WHITE_LENGTH = 1.1;
    const double NUKE_ORANGE_LENGTH = 0.8;
    const double NUKE_BAND_BLEND = 0.25;
    const double NUKE_LEAD_EDGE = 0.15;
    const double NUKE_TAIL_EDGE = 0.5;
    const double NUKE_NOISE = 0.5;
    const double NUKE_HUE_NOISE = 0.35;
    const double NUKE_END_FADE_FROM = 0.88;

    // The scud storm: nine missiles, as the building fires, each one a streak
    // from the top left to where it lands, then a flash, a ring and a pool.
    const int SCUD_COUNT = 9;
    const double SCUD_FIRST_LANDING = 0.15;
    const double SCUD_LIFE = 1.0;
    const double SCUD_LANDING_JITTER = 0.10;
    const double SCUD_FLASH_SECONDS = 0.18;

    // The particle cannon: the beam comes down on the ground, so what the board
    // shows is the spot it lands on. A white point charges in the top left
    // corner, wanders the keyboard, throws blue off itself as it goes, and
    // leaves a blue fire burning down along where it has been.
    const double LASER_CHARGE_SECONDS = 0.4;
    const double LASER_ROAM_SECONDS = 3.0;
    const double LASER_PASSES_ACROSS = 3.0;		// half swings left to right over the roam
    const double LASER_PASSES_DOWN = 5.0;		// and top to bottom, so the two never line up
    const double LASER_CORE_RADIUS = 0.16;
    const double LASER_GLOW_RADIUS = 0.50;
    const int LASER_TRAIL_SAMPLES = 12;
    const double LASER_TRAIL_SECONDS = 1.0;
    const double LASER_TRAIL_RADIUS = 0.30;
    const double LASER_SPRAY_EVERY = 0.12;
    const double LASER_SPRAY_LIFE = 0.45;
    const int LASER_SPRAY_ALIVE = 5;
    const double LASER_SPRAY_SPEED = 1.5;
    const double LASER_SPRAY_LENGTH = 0.22;
    const double LASER_SPRAY_REACH = 0.70;
    const double SCUD_FLASH_RADIUS = 0.35;
    const double SCUD_RING_SPEED = 1.1;
    const double SCUD_RING_LENGTH = 0.30;
    const double SCUD_REACH = 1.0;
    const double SCUD_POOL_RADIUS = 0.45;
    const double SCUD_POOL_LEVEL = 0.35;
    const double ACROSS_EXTENT = 2.0;

    const double TWO_PI = 6.283185307;

    static readonly string[] Endpoints = { "keyboard", "mouse", "mousepad" };
    static readonly string[] EffectNames = { "CHROMA_CUSTOM", "CHROMA_CUSTOM2", "CHROMA_CUSTOM" };
    static readonly int[] Rows = { 6, 9, 1 };
    static readonly int[] Columns = { 22, 7, 15 };
    static readonly int[] FirstCell = { 0, 132, 195 };

    static uint Hash(uint value)
    {
        unchecked
        {
            value ^= value >> 16;
            value *= 0x7feb352du;
            value ^= value >> 15;
            value *= 0x846ca68bu;
            value ^= value >> 16;
            return value;
        }
    }

    static double HashUnit(uint value)
    {
        return (Hash(value) & 0xFFFF) / 65535.0;
    }

    static int Color(double red, double green, double blue)
    {
        int r = (int)(Math.Max(0.0, Math.Min(1.0, red)) * 255.0 + 0.5);
        int g = (int)(Math.Max(0.0, Math.Min(1.0, green)) * 255.0 + 0.5);
        int b = (int)(Math.Max(0.0, Math.Min(1.0, blue)) * 255.0 + 0.5);
        return (b << 16) | (g << 8) | r;
    }

    static double Ripple(double distance, double radius, double wavelength, double reach)
    {
        if (radius <= 0.0 || distance > radius || distance >= reach)
            return 0.0;
        double phase = (radius - distance) / wavelength;
        double crest = 0.5 + 0.5 * Math.Cos(phase * TWO_PI);
        return crest * (1.0 - distance / reach);
    }

    static double SingleWave(double distance, double radius, double length, double fadeAt)
    {
        double offset = radius - distance;
        if (offset < 0.0 || offset > length || distance >= fadeAt)
            return 0.0;
        double crest = 0.5 - 0.5 * Math.Cos(offset / length * TWO_PI);
        return crest * (1.0 - distance / fadeAt);
    }

    // Where the cannon's spot is this long into its roam. It starts in the top
    // left corner and swings across and down at different rates, so it covers
    // the board without retracing itself.
    static void LaserPoint(double roamTime, double downExtent, out double x, out double y)
    {
        double along = Math.Max(0.0, Math.Min(1.0, roamTime / LASER_ROAM_SECONDS));
        x = ACROSS_EXTENT * (0.5 - 0.5 * Math.Cos(along * Math.PI * LASER_PASSES_ACROSS));
        y = downExtent * (0.5 - 0.5 * Math.Cos(along * Math.PI * LASER_PASSES_DOWN));
    }

    static int EffectColor(string effect, double across, double down, double downExtent,
        int cellIndex, int frame, int durationFrames, uint seed)
    {
        double elapsed = (double)frame / LOGIC_FPS;
        double life = (double)frame / durationFrames;
        double fade = 1.0 - life;

        if (effect == "nuke")
        {
            double distance = Math.Sqrt(across * across + down * down);

            // The body: how far behind its front this lamp is decides the band.
            double red = 0.0, green = 0.0, blue = 0.0;
            double bodyLength = NUKE_RED_LENGTH + NUKE_WHITE_LENGTH + NUKE_ORANGE_LENGTH;
            double behind = elapsed * NUKE_BODY_SPEED - distance;
            if (behind > 0.0 && behind < bodyLength)
            {
                double redEnd = NUKE_RED_LENGTH;
                double whiteEnd = NUKE_RED_LENGTH + NUKE_WHITE_LENGTH;
                double half = NUKE_BAND_BLEND * 0.5;

                // red (1, .05, 0) -> white (1, 1, 1) -> orange (1, .45, 0)
                double bandGreen, bandBlue;
                if (behind < redEnd - half) { bandGreen = 0.05; bandBlue = 0.0; }
                else if (behind < redEnd + half)
                {
                    double mix = (behind - (redEnd - half)) / NUKE_BAND_BLEND;
                    bandGreen = 0.05 + mix * 0.95; bandBlue = mix;
                }
                else if (behind < whiteEnd - half) { bandGreen = 1.0; bandBlue = 1.0; }
                else if (behind < whiteEnd + half)
                {
                    double mix = (behind - (whiteEnd - half)) / NUKE_BAND_BLEND;
                    bandGreen = 1.0 - mix * 0.55; bandBlue = 1.0 - mix;
                }
                else { bandGreen = 0.45; bandBlue = 0.0; }

                double envelope = Math.Min(1.0, behind / NUKE_LEAD_EDGE)
                                * Math.Min(1.0, (bodyLength - behind) / NUKE_TAIL_EDGE);
                // The colours breathe: brightness on one hash, the hue on another,
                // stepped every other frame so it shimmers rather than buzzes.
                double noise = 1.0 - NUKE_NOISE * 0.5
                             + NUKE_NOISE * HashUnit(unchecked((uint)cellIndex * 2654435761u + (uint)(frame / 2)));
                double hue = 1.0 - NUKE_HUE_NOISE * 0.5
                           + NUKE_HUE_NOISE * HashUnit(unchecked((uint)cellIndex * 40503u + (uint)(frame / 3) * 7919u));
                red = envelope * noise;
                green = bandGreen * envelope * noise * hue;
                blue = bandBlue * envelope * noise * (2.0 - hue);
            }

            // The shock: thin, white, fast, out in front of all of it.
            double shock = SingleWave(distance, elapsed * NUKE_SHOCK_SPEED, NUKE_SHOCK_LENGTH, 99.0);
            red = Math.Max(red, shock);
            green = Math.Max(green, shock);
            blue = Math.Max(blue, shock);

            if (life > NUKE_END_FADE_FROM)
            {
                double ending = 1.0 - (life - NUKE_END_FADE_FROM) / (1.0 - NUKE_END_FADE_FROM);
                red *= ending; green *= ending; blue *= ending;
            }
            return Color(red, green, blue);
        }

        if (effect == "laser")
        {
            double roamTime = elapsed - LASER_CHARGE_SECONDS;
            double lr = 0.0, lg = 0.0, lb = 0.0;
            double px, py;

            if (roamTime > 0.0)
            {
                // Where the point has been is on fire. The path has no inverse, so
                // it is walked backwards a tenth of a second at a time and the
                // hottest sample near this lamp wins.
                double hottest = 0.0;
                for (int sample = 1; sample <= LASER_TRAIL_SAMPLES; ++sample)
                {
                    double back = sample * (LASER_TRAIL_SECONDS / LASER_TRAIL_SAMPLES);
                    double at = roamTime - back;
                    if (at < 0.0) break;
                    if (at > LASER_ROAM_SECONDS) continue;
                    LaserPoint(at, downExtent, out px, out py);
                    double tx = across - px, ty = down - py;
                    double heat = (1.0 - Math.Sqrt(tx * tx + ty * ty) / LASER_TRAIL_RADIUS)
                                * (1.0 - back / LASER_TRAIL_SECONDS);
                    if (heat > hottest) hottest = heat;
                }
                if (hottest > 0.0)
                {
                    double flicker = HashUnit(unchecked((uint)cellIndex * 2654435761u + (uint)(frame / 2)));
                    double body = hottest * (0.45 + 0.55 * flicker);
                    lr = body * body * body * 0.25; lg = body * body * 0.75; lb = body;
                }

                // The blue it throws off: a small ring born at the point every so
                // often, left behind where it was born while the point moves on.
                int newest = (int)Math.Floor(Math.Min(roamTime, LASER_ROAM_SECONDS) / LASER_SPRAY_EVERY);
                for (int back = 0; back < LASER_SPRAY_ALIVE; ++back)
                {
                    int ring = newest - back;
                    if (ring < 0) break;
                    double born = ring * LASER_SPRAY_EVERY;
                    double age = roamTime - born;
                    if (age < 0.0 || age >= LASER_SPRAY_LIFE) continue;
                    LaserPoint(born, downExtent, out px, out py);
                    double rx = across - px, ry = down - py;
                    double spark = HashUnit(unchecked((uint)cellIndex * 40503u + (uint)ring * 7919u));
                    double spray = SingleWave(Math.Sqrt(rx * rx + ry * ry), age * LASER_SPRAY_SPEED,
                                              LASER_SPRAY_LENGTH, LASER_SPRAY_REACH)
                                 * (1.0 - age / LASER_SPRAY_LIFE) * (0.6 + 0.4 * spark);
                    if (spray > 0.0)
                    {
                        lr = Math.Max(lr, spray * 0.15); lg = Math.Max(lg, spray * 0.6); lb = Math.Max(lb, spray);
                    }
                }
            }

            if (roamTime < LASER_ROAM_SECONDS)
            {
                // The point itself: coming up to strength in the corner, then off.
                double strength = roamTime <= 0.0 ? elapsed / LASER_CHARGE_SECONDS : 1.0;
                LaserPoint(Math.Max(roamTime, 0.0), downExtent, out px, out py);
                double cx = across - px, cy = down - py;
                double off = Math.Sqrt(cx * cx + cy * cy);

                double glow = (1.0 - off / LASER_GLOW_RADIUS) * 0.7 * strength;
                if (glow > 0.0)
                {
                    lr = Math.Max(lr, glow * 0.1); lg = Math.Max(lg, glow * 0.4); lb = Math.Max(lb, glow);
                }

                double shake = HashUnit(unchecked((uint)cellIndex * 40503u + (uint)frame * 7919u));
                double core = (1.0 - off / LASER_CORE_RADIUS) * strength * (0.8 + 0.2 * shake);
                if (core > 0.0)
                {
                    lr = Math.Max(lr, core); lg = Math.Max(lg, core); lb = Math.Max(lb, core);
                }
            }
            return Color(lr, lg, lb);
        }

        // Nine missiles. Each flies in from the top left as a streak, lands, and
        // leaves a flash, a ring and a pool of something green behind it.
        double lastLanding = (double)durationFrames / LOGIC_FPS - SCUD_LIFE;
        double spacing = (lastLanding - SCUD_FIRST_LANDING) / (SCUD_COUNT - 1);
        double sr = 0.0, sg = 0.0, sb = 0.0;
        for (int scud = 0; scud < SCUD_COUNT; ++scud)
        {
            uint scudSeed = unchecked(seed + (uint)scud * 0x9e3779b9u);
            double landing = SCUD_FIRST_LANDING + scud * spacing
                           + (HashUnit(scudSeed + 2) - 0.5) * 2.0 * SCUD_LANDING_JITTER;
            double targetAcross = (0.08 + 0.84 * HashUnit(scudSeed)) * ACROSS_EXTENT;
            double targetDown = HashUnit(scudSeed + 1) * downExtent;

            double age = elapsed - landing;
            if (age < 0.0 || age >= SCUD_LIFE)
                continue;

            double dx = across - targetAcross, dy = down - targetDown;
            double distance = Math.Sqrt(dx * dx + dy * dy);
            double dying = 1.0 - age / SCUD_LIFE;

            if (age < SCUD_FLASH_SECONDS)
            {
                double flash = (1.0 - age / SCUD_FLASH_SECONDS) * (1.0 - distance / SCUD_FLASH_RADIUS);
                if (flash > 0.0)
                {
                    sr = Math.Max(sr, flash * 0.8); sg = Math.Max(sg, flash); sb = Math.Max(sb, flash * 0.8);
                }
            }

            double ring = SingleWave(distance, age * SCUD_RING_SPEED, SCUD_RING_LENGTH, SCUD_REACH) * dying;
            if (ring > 0.0)
            {
                sr = Math.Max(sr, ring * 0.1); sg = Math.Max(sg, ring); sb = Math.Max(sb, ring * 0.15);
            }

            double pool = (1.0 - distance / SCUD_POOL_RADIUS) * SCUD_POOL_LEVEL * dying;
            if (pool > 0.0)
            {
                pool *= 0.7 + 0.6 * HashUnit(unchecked((uint)cellIndex * 2654435761u + (uint)(frame / 3)));
                sr = Math.Max(sr, pool * 0.05); sg = Math.Max(sg, pool * 0.8); sb = Math.Max(sb, pool * 0.1);
            }
        }
        return Color(sr, sg, sb);
    }

    static string BuildBody(int device, string effect, int frame, int durationFrames, uint seed)
    {
        int rows = Rows[device], columns = Columns[device];
        bool nested = rows > 1;
        double halfWidth = (columns - 1) * 0.5;
        double downExtent = halfWidth > 0.0 ? (rows - 1) / halfWidth : 0.0;

        StringBuilder body = new StringBuilder(2048);
        body.Append("{\"effect\":\"").Append(EffectNames[device]).Append("\",\"param\":[");
        for (int row = 0; row < rows; ++row)
        {
            if (nested) body.Append(row == 0 ? "[" : ",[");
            for (int column = 0; column < columns; ++column)
            {
                double across = halfWidth > 0.0 ? column / halfWidth : 0.0;
                double down = halfWidth > 0.0 ? row / halfWidth : 0.0;
                int cell = FirstCell[device] + row * columns + column;
                bool first = nested ? column == 0 : (row == 0 && column == 0);
                if (!first) body.Append(',');
                body.Append(EffectColor(effect, across, down, downExtent, cell, frame, durationFrames, seed));
            }
            if (nested) body.Append(']');
        }
        body.Append("]}");
        return body.ToString();
    }

    public static string Play(string[] effects, int rounds, int gapMs)
    {
        StringBuilder log = new StringBuilder();
        using (HttpClient client = new HttpClient())
        {
            string init = "{\"title\":\"Zero Hour Reforged effect preview\",\"description\":\"Superweapon effect preview\","
                        + "\"author\":{\"name\":\"Zero Hour Reforged\",\"contact\":\"local\"},"
                        + "\"device_supported\":[\"keyboard\",\"mouse\",\"mousepad\"],\"category\":\"application\"}";
            string reply = client.PostAsync("http://localhost:54235/razer/chromasdk",
                new StringContent(init, Encoding.UTF8, "application/json")).Result.Content.ReadAsStringAsync().Result;
            int at = reply.IndexOf("http://");
            string root = reply.Substring(at, reply.IndexOf('"', at) - at);
            log.AppendLine("session: " + root);

            try
            {
                for (int round = 1; round <= rounds; ++round)
                {
                    foreach (string effect in effects)
                    {
                        int durationFrames = effect == "scud" ? 5 * LOGIC_FPS
                                           : effect == "laser" ? 9 * LOGIC_FPS / 2
                                           : 4 * LOGIC_FPS;
                        uint seed = Hash((uint)(round * 977 + effect.Length));
                        DateTime start = DateTime.UtcNow;
                        int lastFrame = -1, sent = 0;
                        while (true)
                        {
                            int frame = (int)((DateTime.UtcNow - start).TotalSeconds * LOGIC_FPS);
                            if (frame >= durationFrames) break;
                            if (frame == lastFrame) { Thread.Sleep(2); continue; }
                            lastFrame = frame;
                            for (int device = 0; device < Endpoints.Length; ++device)
                            {
                                string body = BuildBody(device, effect, frame, durationFrames, seed);
                                client.PutAsync(root + "/" + Endpoints[device],
                                    new StringContent(body, Encoding.UTF8, "application/json")).Result.Dispose();
                            }
                            ++sent;
                        }
                        log.AppendLine(string.Format("round {0} {1}: {2} frames sent, {3:0.0} a second",
                            round, effect, sent, sent / ((double)durationFrames / LOGIC_FPS)));
                        Thread.Sleep(gapMs);
                    }
                }
            }
            finally
            {
                try { client.DeleteAsync(root).Result.Dispose(); } catch { }
            }
        }
        return log.ToString();
    }
}
'@

Add-Type -TypeDefinition $source -ReferencedAssemblies 'System.Net.Http'
[ChromaPreview]::Play($Effects, $Rounds, 1500)
