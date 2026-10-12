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

// {transcript} + {silences}. Output JSON array of suggested cuts:
// {"start":"HH:MM:SSFF","end":"HH:MM:SSFF","reason":"silence|filler","confidence":0.0-1.0}
inline QString suggestCuts()
{
    return QStringLiteral(
        "You are an edit assistant.\n"
        "Suggest cuts for filler words and long silences. Only suggest cuts that\n"
        "remove no meaningful content. Confidence must reflect certainty.\n"
        "Output JSON only: [{\"start\":\"HH:MM:SSFF\",\"end\":\"HH:MM:SSFF\","
        "\"reason\":\"silence|filler\",\"confidence\":0.0}]\n"
        "Transcript:\n{transcript}\nSilence map:\n{silences}");
}

// NL command bar: strictly validated JSON action list (see actionvalidator.h).
inline QString naturalLanguageToActions()
{
    return QStringLiteral(
        "You are Arena Edit's command interpreter.\n"
        "Convert the user request into a JSON array of actions. Valid actions:\n"
        "{\"op\":\"add_transition\",\"target\":{\"type\":\"clip\",\"index\":N},\"transition\":\"cross_dissolve|wipe|push|fade_to_black\",\"duration_ms\":500}\n"
        "{\"op\":\"set_clip_speed\",\"target\":{\"type\":\"clip\",\"index\":N},\"speed\":1.5}\n"
        "{\"op\":\"add_marker\",\"time\":0,\"label\":\"...\"}\n"
        "{\"op\":\"export\",\"preset\":\"youtube_1080p|instagram_reels|web_720p|prores_master|audio_podcast|webm_web\",\"subtitles\":\"burn|sidecar|none\"}\n"
        "Clip index -1 means the last clip. Output the JSON array only, nothing else.\n"
        "User request: {request}");
}

} // namespace prompts
} // namespace arena
