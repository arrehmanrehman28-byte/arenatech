#!/usr/bin/env python3
"""Inject the M6.2 i18n keys into all eight catalogs, preserving formatting."""
import json
import re

NEW = {
    "en": {
        "panel.ai.applied": "Applied",
        "panel.ai.apply.chapters": "Added {count} chapter markers.",
        "panel.ai.apply.cuts": "Applied {count} cut(s) — undo to restore.",
        "panel.ai.apply.title": "Project renamed.",
        "panel.ai.apply.translation": "Added {count} translated cues as a new {lang} track (original kept).",
        "panel.ai.batch.failed": "Translation batch {n} failed after a retry; the result was not applied.",
        "panel.ai.batch.running": "Translating entries {first}–{last} (batch {n} of {batches})…",
        "panel.ai.description.label": "Description",
        "panel.ai.needsProject": "Open or create a project first.",
        "panel.ai.needsSubtitles": "Add caption cues first — AI works on text only.",
        "panel.ai.noVideoTrack": "No video track to cut.",
        "panel.ai.preview.chapters": "{count} chapters proposed:",
        "panel.ai.preview.cuts": "{count} cut(s) suggested. Each is applied as its own undoable edit:",
        "panel.ai.preview.titles": "Suggested metadata:",
        "panel.ai.preview.translate": "{count} cues ready to be added as a new {lang} track. The original track is kept.",
        "panel.ai.reason.filler": "filler",
        "panel.ai.reason.silence": "silence",
        "panel.ai.task.chapters.hint": "Proposes 3–10 chapters from the caption transcript and adds them as labeled markers.",
        "panel.ai.task.cuts.hint": "Suggests removable filler and silence ranges from the captions and gap map. Each accepted cut is an undoable ripple edit on the video track.",
        "panel.ai.task.run": "Run",
        "panel.ai.task.sourceTrack": "Source track",
        "panel.ai.task.targetLang": "Target language",
        "panel.ai.task.titles.hint": "Writes a platform title (max 60 characters) and description (max 300) from project metadata and captions.",
        "panel.ai.task.translate.hint": "Translates the chosen caption track into the target language. The original track is kept; the translation becomes a new track.",
        "panel.ai.title.label": "Title",
        "panel.ai.translationStale": "The source track changed since the request — re-run the translation.",
        "subtitles.trackAdded": "subtitle track added",
        "timeline.chaptersAdded": "chapter markers added",
        "timeline.rangeCut": "range cut",
    },
    "ur": {
        "panel.ai.applied": "لاگو ہو گیا",
        "panel.ai.apply.chapters": "{count} باب کے نشان شامل کر دیے گئے۔",
        "panel.ai.apply.cuts": "{count} کٹ لاگو ہوئے — واپسی کے لیے Undo دبائیں۔",
        "panel.ai.apply.title": "پروژیکٹ کا نام تبدیل کر دیا گیا۔",
        "panel.ai.apply.translation": "{count} ترجمہ شدہ سطحوں کو نئی {lang} ٹریک کے طور پر شامل کیا گیا (اصل ٹریک محفوظ ہے)۔",
        "panel.ai.batch.failed": "ترجمے کی بیچ {n} واپسی کے بعد ناکام رہی؛ نتیجہ لاگو نہیں ہوا۔",
        "panel.ai.batch.running": "الفاظ {first}–{last} کا ترجمہ ہو رہا ہے (بیچ {n} از {batches})…",
        "panel.ai.description.label": "تفصیل",
        "panel.ai.needsProject": "پہلے کسی پروژیکٹ کو کھولیں یا بنائیں۔",
        "panel.ai.needsSubtitles": "پہلے کپشن شامل کریں — AI صرف متن پر کام کرتا ہے۔",
        "panel.ai.noVideoTrack": "کٹ کے لیے کوئی ویڈیو ٹریک موجود نہیں۔",
        "panel.ai.preview.chapters": "{count} باب کی تجویز:",
        "panel.ai.preview.cuts": "{count} کٹس کی تجویز۔ ہر کٹ الگ قابلِ واپسی ترمیم کے طور پر لاگو ہوتی ہے:",
        "panel.ai.preview.titles": "تجویزی میٹاڈیٹا:",
        "panel.ai.preview.translate": "{count} سطحوں کو نئی {lang} ٹریک کے طور پر شامل کیا جا سکتا ہے۔ اصل ٹریک محفوظ رہے گی۔",
        "panel.ai.reason.filler": "پُر کرنے والی باتیں",
        "panel.ai.reason.silence": "خاموشی",
        "panel.ai.task.chapters.hint": "کپشن ٹرانسکرپٹ سے 3 تا 10 باب تجویز کرتا ہے اور انہیں نام شدہ نشانوں کے طور پر شامل کرتا ہے۔",
        "panel.ai.task.cuts.hint": "کپشنز اور خلا کے نقشے سے ہٹا سکنے والی خاموشی اور بول چال کے حصے تجویز کرتا ہے۔ ہر منظور شدہ کٹ ویڈیو ٹریک پر قابلِ واپسی ترمیم ہے۔",
        "panel.ai.task.run": "چلائیں",
        "panel.ai.task.sourceTrack": "ماخذ ٹریک",
        "panel.ai.task.targetLang": "ہدف زبان",
        "panel.ai.task.titles.hint": "پروژیکٹ کی معلومات اور کپشنز سے پلیٹ فارم کے لیے عنوان (زیادہ سے زیادہ 60 حروف) اور تفصیل (زیادہ سے زیادہ 300 حروف) لکھتا ہے۔",
        "panel.ai.task.translate.hint": "منتخب کپشن ٹریک کو ہدف زبان میں ترجمہ کرتا ہے۔ اصل ٹریک محفوظ رہتی ہے؛ ترجمہ نئی ٹریک بنتا ہے۔",
        "panel.ai.title.label": "عنوان",
        "panel.ai.translationStale": "درخواست کے بعد ماخذ ٹریک بدل گئی — ترجمہ دوبارہ چلائیں۔",
        "subtitles.trackAdded": "کپشن ٹریک شامل ہوئی",
        "timeline.chaptersAdded": "باب کے نشان شامل ہوئے",
        "timeline.rangeCut": "رینج کٹ",
    },
    "ar": {
        "panel.ai.applied": "تم تطبيقه",
        "panel.ai.apply.chapters": "أُضيف {count} من علامات الفصول.",
        "panel.ai.apply.cuts": "طُبّق {count} من القصّات — أعد التراجع لاستعادتها.",
        "panel.ai.apply.title": "أعيد تسمية المشروع.",
        "panel.ai.apply.translation": "أُضيف {count} من الأسطر المترجمة كمسار {lang} جديد (المسار الأصلي محفوظ).",
        "panel.ai.batch.failed": "فشل دفعة الترجمة {n} بعد إعادة المحاولة؛ لم يُطبَّق الناتج.",
        "panel.ai.batch.running": "جارٍ ترجمة الأسطر {first}–{last} (الدفعة {n} من {batches})…",
        "panel.ai.description.label": "الوصف",
        "panel.ai.needsProject": "افتح مشروعًا أو أنشئ واحدًا أولًا.",
        "panel.ai.needsSubtitles": "أضِف أسطر الترجمة أولًا — يعمل الذكاء الاصطناعي على النص فقط.",
        "panel.ai.noVideoTrack": "لا يوجد مسار فيديو للقصّ.",
        "panel.ai.preview.chapters": "تم اقتراح {count} فصول:",
        "panel.ai.preview.cuts": "تم اقتراح {count} من القصّات. تُطبَّق كل واحدة كتعديل قابل للتراجع:",
        "panel.ai.preview.titles": "بيانات وصفية مقترحة:",
        "panel.ai.preview.translate": "{count} سطرًا جاهز للإضافة كمسار {lang} جديد. يبقى المسار الأصلي.",
        "panel.ai.reason.filler": "كلمات حشو",
        "panel.ai.reason.silence": "صمت",
        "panel.ai.task.chapters.hint": "يقترح 3–10 فصول من نص التسميات ويضيفها كعلامات موسومة.",
        "panel.ai.task.cuts.hint": "يقترح مقاطع حشو وصمت يمكن حذفها من التسميات وخريطة الفجوات. كل قطعة مقبولة هي تعديل قابل للتراجع على مسار الفيديو.",
        "panel.ai.task.run": "تشغيل",
        "panel.ai.task.sourceTrack": "المسار المصدر",
        "panel.ai.task.targetLang": "اللغة المستهدفة",
        "panel.ai.task.titles.hint": "يكتب عنوانًا (بحد أقصى 60 حرفًا) ووصفًا (بحد أقصى 300 حرف) من بيانات المشروع والتسميات.",
        "panel.ai.task.translate.hint": "يترجم مسار التسميات المحدد إلى اللغة المستهدفة. يبقى المسار الأصلي، وتصبح الترجمة مسارًا جديدًا.",
        "panel.ai.title.label": "العنوان",
        "panel.ai.translationStale": "تغيّر المسار المصدر منذ الطلب — أعد تشغيل الترجمة.",
        "subtitles.trackAdded": "أُضيف مسار تسميات",
        "timeline.chaptersAdded": "أُضيفت علامات الفصول",
        "timeline.rangeCut": "قصّ نطاق",
    },
    "es": {
        "panel.ai.applied": "Aplicado",
        "panel.ai.apply.chapters": "Se añadieron {count} marcadores de capítulos.",
        "panel.ai.apply.cuts": "Se aplicaron {count} corte(s); usa deshacer para restaurar.",
        "panel.ai.apply.title": "Proyecto renombrado.",
        "panel.ai.apply.translation": "Se añadieron {count} subtítulos traducidos como nueva pista {lang} (la original se conserva).",
        "panel.ai.batch.failed": "El lote de traducción {n} falló tras el reintento; no se aplicó el resultado.",
        "panel.ai.batch.running": "Traduciendo las entradas {first}–{last} (lote {n} de {batches})…",
        "panel.ai.description.label": "Descripción",
        "panel.ai.needsProject": "Abre o crea un proyecto primero.",
        "panel.ai.needsSubtitles": "Añade subtítulos primero: la IA solo trabaja con texto.",
        "panel.ai.noVideoTrack": "No hay pista de vídeo que cortar.",
        "panel.ai.preview.chapters": "{count} capítulos propuestos:",
        "panel.ai.preview.cuts": "{count} corte(s) sugerido(s). Cada uno se aplica como edición independiente y deshacible:",
        "panel.ai.preview.titles": "Metadatos sugeridos:",
        "panel.ai.preview.translate": "{count} líneas listas para añadirse como nueva pista {lang}. La pista original se conserva.",
        "panel.ai.reason.filler": "relleno",
        "panel.ai.reason.silence": "silencio",
        "panel.ai.task.chapters.hint": "Propone 3–10 capítulos a partir del texto de los subtítulos y los añade como marcadores con nombre.",
        "panel.ai.task.cuts.hint": "Sugiere rangos de relleno y silencio eliminables según los subtítulos y el mapa de huecos. Cada corte aceptado es una edición deshacible en la pista de vídeo.",
        "panel.ai.task.run": "Ejecutar",
        "panel.ai.task.sourceTrack": "Pista de origen",
        "panel.ai.task.targetLang": "Idioma destino",
        "panel.ai.task.titles.hint": "Escribe un título (máx. 60 caracteres) y una descripción (máx. 300) a partir de los metadatos y los subtítulos.",
        "panel.ai.task.translate.hint": "Traduce la pista de subtítulos elegida al idioma destino. La pista original se conserva; la traducción crea una pista nueva.",
        "panel.ai.title.label": "Título",
        "panel.ai.translationStale": "La pista de origen cambió desde la petición: vuelve a ejecutar la traducción.",
        "subtitles.trackAdded": "pista de subtítulos añadida",
        "timeline.chaptersAdded": "marcadores de capítulos añadidos",
        "timeline.rangeCut": "corte de rango",
    },
    "fr": {
        "panel.ai.applied": "Appliqué",
        "panel.ai.apply.chapters": "{count} marqueurs de chapitres ajoutés.",
        "panel.ai.apply.cuts": "{count} coupure(s) appliquée(s) — Annuler pour restaurer.",
        "panel.ai.apply.title": "Projet renommé.",
        "panel.ai.apply.translation": "{count} sous-titres traduits ajoutés comme nouvelle piste {lang} (l'originale est conservée).",
        "panel.ai.batch.failed": "Le lot de traduction {n} a échoué après nouvelle tentative ; le résultat n'a pas été appliqué.",
        "panel.ai.batch.running": "Traduction des entrées {first}–{last} (lot {n} sur {batches})…",
        "panel.ai.description.label": "Description",
        "panel.ai.needsProject": "Ouvrez ou créez d'abord un projet.",
        "panel.ai.needsSubtitles": "Ajoutez des sous-titres d'abord — l'IA ne travaille que sur du texte.",
        "panel.ai.noVideoTrack": "Aucune piste vidéo à couper.",
        "panel.ai.preview.chapters": "{count} chapitres proposés :",
        "panel.ai.preview.cuts": "{count} coupure(s) suggérée(s). Chacune est appliquée comme édition annulable :",
        "panel.ai.preview.titles": "Métadonnées suggérées :",
        "panel.ai.preview.translate": "{count} lignes prêtes à être ajoutées comme nouvelle piste {lang}. La piste originale est conservée.",
        "panel.ai.reason.filler": "bourrage",
        "panel.ai.reason.silence": "silence",
        "panel.ai.task.chapters.hint": "Propose 3 à 10 chapitres à partir des sous-titres et les ajoute comme marqueurs nommés.",
        "panel.ai.task.cuts.hint": "Suggère des plages de bourrage et de silence supprimables d'après les sous-titres et la carte des silences. Chaque coupure acceptée est une édition annulable sur la piste vidéo.",
        "panel.ai.task.run": "Exécuter",
        "panel.ai.task.sourceTrack": "Piste source",
        "panel.ai.task.targetLang": "Langue cible",
        "panel.ai.task.titles.hint": "Rédige un titre (60 caractères max) et une description (300 max) à partir des métadonnées et des sous-titres.",
        "panel.ai.task.translate.hint": "Traduit la piste de sous-titres choisie dans la langue cible. La piste originale est conservée ; la traduction crée une nouvelle piste.",
        "panel.ai.title.label": "Titre",
        "panel.ai.translationStale": "La piste source a changé depuis la demande — relancez la traduction.",
        "subtitles.trackAdded": "piste de sous-titres ajoutée",
        "timeline.chaptersAdded": "marqueurs de chapitres ajoutés",
        "timeline.rangeCut": "coupure de plage",
    },
    "hi": {
        "panel.ai.applied": "प्रयुक्त हुआ",
        "panel.ai.apply.chapters": "{count} अध्याय चिह्न जोड़े गए।",
        "panel.ai.apply.cuts": "{count} कट लागू हुए — पুনर्स्थापित करने के लिए Undo दबाएँ।",
        "panel.ai.apply.title": "प्रोज़ेक्ट का नाम बदल दिया गया।",
        "panel.ai.apply.translation": "{count} अनुवादित पंक्तियाँ नई {lang} ट्रैक के रूप में जोड़ी गईं (मूल ट्रैक सुरक्षित है)।",
        "panel.ai.batch.failed": "अनुवाद बैच {n} पुनः प्रयास के बाद विफल रहा; परिणाम लागू नहीं हुआ।",
        "panel.ai.batch.running": "प्रविष्टियाँ {first}–{last} अनुवादित हो रही हैं (बैच {n} में से {batches})…",
        "panel.ai.description.label": "विवरण",
        "panel.ai.needsProject": "पहले कोई प्रोज़ेक्ट खोलें या बनाएँ।",
        "panel.ai.needsSubtitles": "पहले उपशीर्षक जोड़ें — AI केवल पाठ पर काम करता है।",
        "panel.ai.noVideoTrack": "कट के लिए कोई वीडियो ट्रैक नहीं है।",
        "panel.ai.preview.chapters": "{count} अध्यायों का सुझाव:",
        "panel.ai.preview.cuts": "{count} कटों का सुझाव। प्रत्येक को स्वतंत्र पूर्ववर्तनीय संपादन के रूप में लागू किया जाता है:",
        "panel.ai.preview.titles": "सुझाया गया मेटाडेटा:",
        "panel.ai.preview.translate": "{count} पंक्तियाँ नई {lang} ट्रैक के रूप में जोड़ने के लिए तैयार। मूल ट्रैक सुरक्षित रहेगा।",
        "panel.ai.reason.filler": "भराव",
        "panel.ai.reason.silence": "मौन",
        "panel.ai.task.chapters.hint": "उपशीर्षक पाठ से 3–10 अध्याय सुझाता है और उन्हें नामित चिह्नों के रूप में जोड़ता है।",
        "panel.ai.task.cuts.hint": "उपशीर्षकों और अंतर-नक्शे से हटाए जा सकने वाले भराव और मौन के अंतराल सुझाता है। स्वीकृत प्रत्येक कट वीडियो ट्रैक पर पूर्ववर्तनीय संपादन है।",
        "panel.ai.task.run": "चलाएँ",
        "panel.ai.task.sourceTrack": "स्रोत ट्रैक",
        "panel.ai.task.targetLang": "लक्ष्य भाषा",
        "panel.ai.task.titles.hint": "प्रोज़ेक्ट मेटाडेटा और उपशीर्षकों से शीर्षक (अधिकतम 60 अक्षर) और विवरण (अधिकतम 300) लिखता है।",
        "panel.ai.task.translate.hint": "चुनी गई उपशीर्षक ट्रैक को लक्ष्य भाषा में अनुवादित करता है। मूल ट्रैक सुरक्षित रहती है; अनुवाद नई ट्रैक बनती है।",
        "panel.ai.title.label": "शीर्षक",
        "panel.ai.translationStale": "अनुरोध के बाद स्रोत ट्रैक बदल गई है — अनुवाद दोबारा चलाएँ।",
        "subtitles.trackAdded": "उपशीर्षक ट्रैक जोड़ी गई",
        "timeline.chaptersAdded": "अध्याय चिह्न जोड़े गए",
        "timeline.rangeCut": "अंतराल कट",
    },
    "zh": {
        "panel.ai.applied": "已应用",
        "panel.ai.apply.chapters": "已添加 {count} 个章节标记。",
        "panel.ai.apply.cuts": "已应用 {count} 处剪辑 —— 撤销可恢复。",
        "panel.ai.apply.title": "项目已重命名。",
        "panel.ai.apply.translation": "已将 {count} 条翻译字幕作为新的 {lang} 轨道添加（保留原轨道）。",
        "panel.ai.batch.failed": "翻译批次 {n} 重试后仍失败；结果未应用。",
        "panel.ai.batch.running": "正在翻译第 {first}–{last} 条（批次 {n}/{batches}）…",
        "panel.ai.description.label": "描述",
        "panel.ai.needsProject": "请先打开或创建项目。",
        "panel.ai.needsSubtitles": "请先添加字幕 —— AI 只处理文本。",
        "panel.ai.noVideoTrack": "没有可剪辑的视频轨道。",
        "panel.ai.preview.chapters": "建议 {count} 个章节：",
        "panel.ai.preview.cuts": "建议 {count} 处剪辑。每处都将作为独立、可撤销的编辑应用：",
        "panel.ai.preview.titles": "建议的元数据：",
        "panel.ai.preview.translate": "{count} 条字幕可作为新的 {lang} 轨道添加。原轨道将保留。",
        "panel.ai.reason.filler": "语气填充",
        "panel.ai.reason.silence": "静音",
        "panel.ai.task.chapters.hint": "根据字幕文本提出 3–10 个章节，并添加为带标签的标记。",
        "panel.ai.task.cuts.hint": "根据字幕和间隙图建议可删除的填充词与静音区间。每个接受的剪辑都是视频轨道上可撤销的编辑。",
        "panel.ai.task.run": "运行",
        "panel.ai.task.sourceTrack": "源轨道",
        "panel.ai.task.targetLang": "目标语言",
        "panel.ai.task.titles.hint": "根据项目元数据和字幕撰写标题（最多 60 个字符）和描述（最多 300 个字符）。",
        "panel.ai.task.translate.hint": "将所选字幕轨道翻译为目标语言。原轨道保留，译文成为新轨道。",
        "panel.ai.title.label": "标题",
        "panel.ai.translationStale": "源轨道自请求后已变化 —— 请重新运行翻译。",
        "subtitles.trackAdded": "已添加字幕轨道",
        "timeline.chaptersAdded": "已添加章节标记",
        "timeline.rangeCut": "区间剪辑",
    },
    "ja": {
        "panel.ai.applied": "適用しました",
        "panel.ai.apply.chapters": "{count} 個のチャプターマーカーを追加しました。",
        "panel.ai.apply.cuts": "{count} 件のカットを適用しました — 元に戻すで復元できます。",
        "panel.ai.apply.title": "プロジェクト名を変更しました。",
        "panel.ai.apply.translation": "翻訳済みの {count} 行を新しい {lang} トラックとして追加しました（元のトラックは保持）。",
        "panel.ai.batch.failed": "翻訳バッチ {n} は再試行後も失敗しました。結果は適用されませんでした。",
        "panel.ai.batch.running": "エントリ {first}–{last} を翻訳中（バッチ {n}/{batches}）…",
        "panel.ai.description.label": "説明",
        "panel.ai.needsProject": "先にプロジェクトを開くか作成してください。",
        "panel.ai.needsSubtitles": "先に字幕を追加してください — AI はテキストのみを扱います。",
        "panel.ai.noVideoTrack": "カット可能な映像トラックがありません。",
        "panel.ai.preview.chapters": "{count} 個のチャプターを提案：",
        "panel.ai.preview.cuts": "{count} 件のカットを提案。各カットは独立した元に戻せる編集として適用されます：",
        "panel.ai.preview.titles": "提案されたメタデータ：",
        "panel.ai.preview.translate": "{count} 行を新しい {lang} トラックとして追加できます。元のトラックは保持されます。",
        "panel.ai.reason.filler": "フィラー",
        "panel.ai.reason.silence": "無音",
        "panel.ai.task.chapters.hint": "字幕テキストから 3〜10 個のチャプターを提案し、ラベル付きマーカーとして追加します。",
        "panel.ai.task.cuts.hint": "字幕と間隔マップから削除可能なフィラーや無音区間を提案します。承認された各カットは映像トラック上での元に戻せる編集です。",
        "panel.ai.task.run": "実行",
        "panel.ai.task.sourceTrack": "元トラック",
        "panel.ai.task.targetLang": "対象言語",
        "panel.ai.task.titles.hint": "プロジェクトのメタデータと字幕から、タイトル（最大 60 文字）と説明（最大 300 文字）を作成します。",
        "panel.ai.task.translate.hint": "選択した字幕トラックを対象言語に翻訳します。元のトラックは保持され、翻訳は新しいトラックになります。",
        "panel.ai.title.label": "タイトル",
        "panel.ai.translationStale": "要求後に元トラックが変更されました — 翻訳を再実行してください。",
        "subtitles.trackAdded": "字幕トラックを追加",
        "timeline.chaptersAdded": "チャプターマーカーを追加",
        "timeline.rangeCut": "範囲カット",
    },
}

KEY_RE = re.compile(r'^\s{4}"((?:[^"\\]|\\.)*)":')


def inject(path: str, new: dict) -> None:
    lines = open(path, encoding="utf-8").read().split("\n")
    # Identify the line indices that hold strings keys (4-space indent).
    key_lines = []  # (line_idx, key)
    for i, ln in enumerate(lines):
        m = KEY_RE.match(ln)
        if m:
            key_lines.append((i, m.group(1)))
    existing = {k for _, k in key_lines}
    for k, v in new.items():
        if k in existing:
            continue
        # Find the first existing key that sorts after k (among keys in the
        # same dotted family, i.e. sharing the prefix up to the last dot).
        fam = k.rsplit(".", 1)[0]
        idx = len(key_lines)
        for i, (li, kk) in enumerate(key_lines):
            if kk.rsplit(".", 1)[0] == fam and kk > k:
                idx = i
                break
        else:
            # No later sibling: fall back to first later key of any family.
            for i, (li, kk) in enumerate(key_lines):
                if kk > k:
                    idx = i
                    break
        ins_line = '    ' + json.dumps(k, ensure_ascii=False) + ': ' + json.dumps(v, ensure_ascii=False) + ','
        lines.insert(key_lines[idx][0], ins_line)
        key_lines.insert(idx, (key_lines[idx][0], k))
    open(path, "w", encoding="utf-8").write("\n".join(lines))


for loc in NEW:
    inject(f"i18n/{loc}.json", NEW[loc])
    print("injected", loc)

# Validate: every catalog parses, key sets match en, JSON round-trip shape.
en = json.load(open("i18n/en.json", encoding="utf-8"))["strings"]
for loc in NEW:
    d = json.load(open(f"i18n/{loc}.json", encoding="utf-8"))["strings"]
    assert set(d) == set(en), (loc, set(en) ^ set(d))
    assert len(d) == len(en)
print("parity OK, keys:", len(en))
