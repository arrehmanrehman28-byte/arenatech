#pragma once
// Prompt templates for AI automation (phase M6/M7). All prompts demand
// machine-checkable output; results are validated before any UI use and
// only applied after explicit user confirmation.
#include <QString>
#include <QVariantMap>

namespace arena {
namespace prompts {

// {entries} = JSON array of {index, start, end, text}; {targetLang} = BCP-47.
// Output must be a JSON array of exactly {count} strings, same order.
inline QString translateSubtitles()
{
    return QStringLiteral(
        "You are a professional subtitle translator.\n"
        "Translate the following subtitle entries from their source language into {targetLang}.\n"
        "Rules:\n"
        "1. Keep the exact number of entries and their order.\n"
        "2. Preserve line breaks inside each entry.\n"
        "3. Preserve speaker tags like [John] or (whispers) verbatim.\n"
        "4. No explanations. Output a JSON array of strings only.\n"
        "Entries:\n{entries}");
}

// {transcript} = "HH:MM:SSFF text" lines. Output JSON array of
// {start:"HH:MM:SS", title:"..."} with 3-10 chapters.
inline QString generateChapters()
{
    return QStringLiteral(
        "You are a video editor.\n"
        "Propose 3 to 10 chapters for the video below.\n"
        "Each chapter title is at most 6 words, in the user's language.\n"
        "Output JSON only: [{\"start\":\"HH:MM:SS\",\"title\":\"...\"}, ...]\n"
        "Transcript:\n{transcript}");
}

// {context} = title/description metadata. Output JSON: {"title":"...","description":"..."}.
inline QString smartTitleAndDescription()
{
    return QStringLiteral(
        "You are a video metadata writer.\n"
        "Write one title (max 60 chars) and one description (max 300 chars)\n"
        "for a video platform, in the user's language.\n"
        "Output JSON only: {\"title\":\"...\",\"description\":\"...\"}\n"
        "Context:\n{context}");
}

// {transcript} + {silences} + {fps}. Output JSON array of suggested cuts:
// {"start":"HH:MM:SSFF","end":"HH:MM:SSFF","reason":"silence|filler","confidence":0.0-1.0}
// The frame-field rule is stated explicitly: small models otherwise emit
// frame 30 at 30 fps (live finding 2026-10-13, see AI_INTEGRATION.md).
inline QString suggestCuts()
{
    return QStringLiteral(
        "You are an edit assistant.\n"
        "Suggest cuts for filler words and long silences. Only suggest cuts that\n"
        "remove no meaningful content. Confidence must reflect certainty.\n"
        "Timecodes are SMPTE HH:MM:SSFF at {fps} fps: the frame field is the part\n"
        "after the last colon and must be lower than {fps} (00-29 at 30 fps).\n"
        "Every end must be strictly after its start. For a filler word, make the\n"
        "end cover roughly half a second from the start (the spoken word only).\n"
        "Output JSON only: [{\"start\":\"HH:MM:SSFF\",\"end\":\"HH:MM:SSFF\","
        "\"reason\":\"silence|filler\",\"confidence\":0.0}]\n"
        "Transcript:\n{transcript}\nSilence map:\n{silences}");
}

// NL command bar: strictly validated JSON action list (see actionvalidator.h).
// {context} = machine-readable project context (fps, clip list, markers);
// {request} = the user's natural-language command.
inline QString naturalLanguageToActions()
{
    return QStringLiteral(
        "You are Arena Edit's command interpreter.\n"
        "Convert the user request into a JSON array of actions. Valid actions:\n"
        "{\"op\":\"add_transition\",\"target\":{\"type\":\"clip\",\"index\":N},\"transition\":\"cross_dissolve|wipe|push|fade_to_black\"}\n"
        "{\"op\":\"set_clip_speed\",\"target\":{\"type\":\"clip\",\"index\":N},\"speed\":1.5}\n"
        "{\"op\":\"add_marker\",\"time\":FRAMES,\"label\":\"...\"}\n"
        "{\"op\":\"export\",\"preset\":\"youtube_1080p|instagram_reels|web_720p|prores_master|audio_podcast|webm_web\",\"subtitles\":\"burn|sidecar|none\"}\n"
        "Rules:\n"
        "1. Clip indices are 0-based on the main video track; -1 means the last clip.\n"
        "2. \"fade out\" or \"fade to black\" = add_transition with transition \"fade_to_black\".\n"
        "3. add_marker time is integer FRAMES at the project fps (e.g. 300 = 10 s at 30 fps).\n"
        "4. add_transition may carry \"duration_ms\":10..10000; omit it when the user did not\n"
        "   specify a length (the editor then uses a 0.5 s default) — omitting it is not\n"
        "   inventing a value, so do not output [] for that reason.\n"
        "5. speed is 0.1..16; label is at most 120 characters.\n"
        "6. Use only the project context facts; never invent clips or times.\n"
        "6. If the request maps to no valid action, output [].\n"
        "Project context:\n{context}\n"
        "Output the JSON array only, nothing else.\n"
        "User request: {request}");
}

} // namespace prompts
} // namespace arena
