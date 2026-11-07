#include <ranges>

#include "emerick/app/libraries.hpp"

namespace emerick::app {

namespace {

// Big Rusty's 50 is the crash choke and its 55 the ride choke, so the high toms, splash, china and second crash move
// to its own keys for those sounds. Its pan controls spread the kit the way a General MIDI kit pans it, and CC25 to
// CC27 switch the snare, toms and hi-hat to brushes while the cymbals keep their sticks.
LibraryProgram bigRustyKit() {
    return {
        .gmKits = {"Standard", "Room", "Power", "Jazz", "Brush"},
        .keys = {{48, 45}, {49, 49}, {50, 47}, {52, 57}, {55, 65}, {57, 65}},
        .controls = {{17, 84}, {32, 36}, {36, 50}, {40, 72}, {44, 88}, {102, 44}, {106, 86}},
        .kitControls = {{.kit = "Brush", .controls = {{25, 80}, {26, 80}, {27, 80}}}},
        .pieces = {{.name = "kick", .file = "Programs/03-kick.sfz", .gmNotes = {35, 36}},
                   {.name = "snare", .file = "Programs/04-snare.sfz", .gmNotes = {37, 38, 39, 40}},
                   {.name = "toms", .file = "Programs/05-toms.sfz", .gmNotes = {41, 43, 45, 47, 48, 50}},
                   {.name = "hi-hat", .file = "Programs/06-hihat.sfz", .gmNotes = {42, 44, 46}},
                   {.name = "cymbals", .file = "Programs/07-cymbals.sfz", .gmNotes = {49, 51, 52, 53, 55, 57, 59}}}};
}

}  // namespace

std::vector<std::string> libraryIdsToInstall(const std::vector<std::string>& given,
                                             const std::vector<Library>& catalog) {
    const auto everyId = catalog | std::views::transform(&Library::id) | std::ranges::to<std::vector>();
    return given |
           std::views::transform([&](const std::string& id) { return id == "all" ? everyId : std::vector{id}; }) |
           std::views::join | std::ranges::to<std::vector>();
}

std::vector<Library> curatedLibraries() {
    return {
        {.id = "salamander-grand-piano",
         .name = "Salamander Grand Piano",
         .description = "Yamaha C5 grand piano, 16 velocity layers, by Alexander Holm",
         .url = "https://github.com/sfzinstruments/SalamanderGrandPiano/archive/"
                "3382bf9496bba2486f5ab0de55a264d1dfc38404.tar.gz",
         .sha256 = "f36bef84632067387909ba03064daee6e81afdea3f93526c4ee56a22e12613ba",
         .size = 748'441'806,
         .license = "CC-BY-3.0",
         .sfz = "Salamander Grand Piano V3.sfz",
         .programs = {{.gmSounds = {"Acoustic Grand Piano", "Bright Acoustic Piano"}}}},
        {.id = "vsco2-ce",
         .name = "VSCO 2 Community Edition",
         .description = "Orchestra of strings, brass, woodwinds, keys and percussion, by Versilian Studios",
         .url = "https://github.com/sgossner/VSCO-2-CE/archive/refs/tags/1.1.0.tar.gz",
         .sha256 = "ac577fd3bc9171560087c8182ef6072f11ae0b63b3c3af9a5f752b91e26d3b96",
         .size = 2'302'038'139,
         .license = "CC0-1.0",
         .sfz = "",
         .programs =
             {
                 {.file = "UprightPiano.sfz", .gmSounds = {"Honky-tonk Piano"}},
                 {.file = "CelloEnsSusVib.sfz", .gmSounds = {"Cello"}},
                 {.file = "ContrabassSusVB.sfz", .gmSounds = {"Contrabass"}},
                 {.file = "ViolinEnsPizz.sfz", .gmSounds = {"Pizzicato Strings"}},
                 {.file = "Harp.sfz", .gmSounds = {"Orchestral Harp"}},
                 {.file = "FluteSusVib.sfz", .gmSounds = {"Flute"}},
                 {.file = "ClarinetSus.sfz", .gmSounds = {"Clarinet"}},
                 {.file = "OboeSusVib.sfz", .gmSounds = {"Oboe"}},
                 {.file = "BassoonSus.sfz", .gmSounds = {"Bassoon"}},
                 {.file = "TrumpetSusVib.sfz", .gmSounds = {"Trumpet"}},
                 {.file = "TromboneSus.sfz", .gmSounds = {"Trombone"}},
                 {.file = "FHornSus.sfz", .gmSounds = {"French Horn"}},
                 {.file = "TubaSus.sfz", .gmSounds = {"Tuba"}},
                 {.file = "Glockenspiel.sfz", .gmSounds = {"Glockenspiel"}},
                 {.file = "Marimba.sfz", .gmSounds = {"Marimba"}},
                 {.file = "Xylophone.sfz", .gmSounds = {"Xylophone"}},
                 {.file = "TubularBells.sfz", .gmSounds = {"Tubular Bells"}},
                 {.file = "OrganLoud.sfz", .gmSounds = {"Church Organ"}},
                 {.file = "Timpani.sfz", .gmSounds = {"Timpani"}},
             }},
        {.id = "big-rusty-drums",
         .name = "Big Rusty Drums",
         .description = "Acoustic drum kit with velocity layers and round robins, by Karoryfer Samples",
         .url = "https://github.com/sfzinstruments/karoryfer.big-rusty-drums/archive/refs/tags/v1.100.tar.gz",
         .sha256 = "62c5b9ace0d99b0d0925c7018f7835ad042523acb7a7f58bf50eae927507b1f5",
         .size = 619'600'115,
         .license = "CC0-1.0",
         .sfz = "Programs/01-full.sfz",
         .programs = {bigRustyKit()}},
        {.id = "black-and-blue-basses",
         .name = "Black And Blue Basses",
         .description = "Fingered and picked 5-string bass guitars sounding an octave below the played note, by "
                        "Karoryfer Samples",
         .url = "https://github.com/sfzinstruments/karoryfer.black-and-blue-basses/archive/refs/tags/v1.002.tar.gz",
         .sha256 = "d4f5d17fd593a2fe2244a94eb528be4f0631c5fe52be6c771866ebc825f79ff7",
         .size = 1'010'527'131,
         .license = "CC0-1.0",
         .sfz = "Programs",
         .programs =
             {
                 {.file = "Programs/03-babyblue_all.sfz", .gmSounds = {"Electric Bass (finger)"}, .transpose = 12},
                 {.file = "Programs/05-darkblack_pluck.sfz", .gmSounds = {"Electric Bass (pick)"}, .transpose = 12},
             }},
        {.id = "meatbass",
         .name = "Meatbass",
         .description = "Double bass played pizzicato and arco, by Karoryfer Samples",
         .url = "https://github.com/sfzinstruments/karoryfer.meatbass/archive/refs/tags/v1.001.tar.gz",
         .sha256 = "2e28639580ebac1d8c63cec5ccae23251111f51e772f6e3c773876dccde4c17f",
         .size = 255'760'533,
         .license = "CC0-1.0",
         .sfz = "Programs",
         .programs = {{.file = "Programs/04_pizz.sfz", .gmSounds = {"Acoustic Bass"}}}},
        {.id = "lately-bass",
         .name = "Lately Bass",
         .description = "FM synth bass after the Yamaha TX81Z Lately Bass, sounding an octave below the played note, "
                        "by FreePats",
         .url = "https://github.com/freepats/lately-bass/archive/refs/tags/2024-04-09.tar.gz",
         .sha256 = "50f757c6687949077d1d1d31a235bb1ed35f83d6a0aa86a2eee884a585189e0a",
         .size = 2'840'630,
         .license = "CC0-1.0",
         .sfz = "LatelyBass 20240409.sfz",
         .programs = {{.gmSounds = {"Synth Bass 1", "Synth Bass 2"}, .transpose = 12}}},
        {.id = "mtg-solo-sax",
         .name = "MTG Solo Saxophones",
         .description = "Soprano, alto, tenor and baritone saxophones, by the Music Technology Group",
         .url = "https://github.com/sfzinstruments/MTG.SoloSax/archive/b494d256549b3d088fdec176ce82867f8a1f58b2.tar.gz",
         .sha256 = "eacedfda45ad9897798b404ee056c9218df230b7280af9a0e1e9c78b3636c397",
         .size = 104'910'999,
         .license = "CC-BY-4.0",
         .sfz = "MTG Solo Saxophones",
         .programs =
             {
                 {.file = "MTG Solo Saxophones/MTG Soprano Sax.sfz", .gmSounds = {"Soprano Sax"}},
                 {.file = "MTG Solo Saxophones/MTG Alto Sax.sfz", .gmSounds = {"Alto Sax"}},
                 {.file = "MTG Solo Saxophones/MTG Tenor Sax.sfz", .gmSounds = {"Tenor Sax"}},
                 {.file = "MTG Solo Saxophones/MTG Baritone Sax.sfz", .gmSounds = {"Baritone Sax"}},
             }},
        {.id = "greg-sullivan-e-pianos",
         .name = "Greg Sullivan E-Pianos",
         .description = "Wurlitzer EP200, Yamaha CP80 and Hohner Pianet T electric pianos, by Greg Sullivan",
         .url = "https://github.com/sfzinstruments/GregSullivan.E-Pianos/archive/"
                "8c3e581acda3594b553948ff0222d4f84a698376.tar.gz",
         .sha256 = "e88ff3a3536fb559657c2ae4e09ae201e01b60f30f97dec5c4ca98a85f7f7554",
         .size = 19'254'201,
         .license = "CC-BY-3.0",
         .sfz = "",
         .programs =
             {
                 {.file = "CP80/CP80.sfz", .gmSounds = {"Electric Grand Piano"}},
                 {.file = "Wurlitzer EP200/Wurlitzer EP200.sfz", .gmSounds = {"Electric Piano 1", "Electric Piano 2"}},
             }},
        {.id = "black-and-green-guitars",
         .name = "Black And Green Guitars",
         .description = "Gretsch and Hofner clean electric guitars with twang, staccato and hammer-on articulations, "
                        "by Karoryfer Samples",
         .url = "https://github.com/sfzinstruments/karoryfer.black-and-green-guitars/archive/refs/tags/v1.000.tar.gz",
         .sha256 = "0857bba5323ff12d85e9f629db90cee82a17b753ce1ed8262bf37f4b3359a895",
         .size = 483'074'749,
         .license = "CC0-1.0",
         .sfz = "Programs",
         .programs =
             {
                 {.file = "Programs/04-green_twang.sfz",
                  .gmSounds = {"Electric Guitar (clean)", "Electric Guitar (jazz)"}},
                 {.file = "Programs/05-green_staccato.sfz", .gmSounds = {"Electric Guitar (muted)"}},
             }},
        {.id = "distorted-electric-guitar",
         .name = "Distorted Electric Guitar",
         .description = "Overdriven Fender electric guitar, by FreePats",
         .url = "https://github.com/freepats/electric-guitar-FSBS-dist2/archive/refs/tags/2022-09-11.tar.gz",
         .sha256 = "d593ae5ed77744af97b8eaeb0bfecceab7b3d8d8f058ce71bb6ade4a72d99f4a",
         .size = 135'758'267,
         .license = "CC0-1.0",
         .sfz = "EGuitarFSBS-dist2 bridge 20220911.sfz",
         .programs = {{.gmSounds = {"Overdriven Guitar", "Distortion Guitar"}}}},
        {.id = "spanish-classical-guitar",
         .name = "Spanish Classical Guitar",
         .description = "Nylon-string classical guitar, by FreePats",
         .url = "https://github.com/freepats/spanish-classical-guitar/archive/refs/tags/v1.0.0.tar.gz",
         .sha256 = "94a7a046bd4b52c86f60c3a1282312334d1b9196d1bc7db337a464b9bbebdd37",
         .size = 4'955'000,
         .license = "CC0-1.0",
         .sfz = "SpanishClassicalGuitar-20190618.sfz",
         .programs = {{.gmSounds = {"Acoustic Guitar (nylon)"}}}},
        {.id = "synth-strings",
         .name = "Synth Strings",
         .description = "Synthesized string ensemble pad across the whole keyboard, by FreePats",
         .url = "https://github.com/freepats/synth-strings-1/archive/refs/tags/2020-05-28.tar.gz",
         .sha256 = "d108e5bc12b761d7bc23328d53d4fd96476950133facb2adc95fc690ce0c6127",
         .size = 3'812'315,
         .license = "CC0-1.0",
         .sfz = "SynthStrings1 20200528.sfz",
         .programs = {{.gmSounds = {"Synth Strings 1", "Synth Strings 2"}}}},
        {.id = "synth-pad-bowed",
         .name = "Synth Pad Bowed",
         .description = "Bowed synth pad, by FreePats",
         .url = "https://github.com/freepats/synth-pad-bowed/archive/refs/tags/2019-07-19.tar.gz",
         .sha256 = "acbe5228a69aa2f87b43e48ffa45834da89f44d9a9cf520cfa65cb6396475ec1",
         .size = 11'912'568,
         .license = "CC0-1.0",
         .sfz = "SynthPadBowed 20190719.sfz",
         .programs = {{.gmSounds = {"Pad 5 (bowed)", "Pad 2 (warm)"}}}},
        {.id = "sweep-pad",
         .name = "Sweep Pad",
         .description = "Sweep synth pad, by FreePats",
         .url = "https://github.com/freepats/sweep-pad/archive/refs/tags/2019-08-13.tar.gz",
         .sha256 = "2f4ab0e973e0e4ee5e2b2ed15f78af8f44c93af06dc9343ea9b51ed86c2c10bb",
         .size = 2'995'334,
         .license = "CC0-1.0",
         .sfz = "SweepPad 20190813.sfz",
         .programs = {{.gmSounds = {"Pad 8 (sweep)"}}}},
        {.id = "synth-square-lead",
         .name = "Synth Square Lead",
         .description = "Square wave synth lead, by FreePats",
         .url = "https://github.com/freepats/synth-square/archive/refs/tags/2020-05-12.tar.gz",
         .sha256 = "5efdc6654e6ad334cff6dbdecd6648cd5ac94ba68a7cb3fef14c9bd345bd24fd",
         .size = 7'280'683,
         .license = "CC0-1.0",
         .sfz = "SynthSquare 20200512.sfz",
         .programs = {{.gmSounds = {"Lead 1 (square)"}}}},
    };
}

}  // namespace emerick::app
