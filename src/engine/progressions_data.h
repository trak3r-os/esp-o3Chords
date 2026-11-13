#pragma once

#include <vector>
#include <string>
#include <map>
#include "progression.h"

namespace engine {

struct ProgressionInfo {
    std::string name;
    std::string description;
    std::vector<ProgressionStep> steps;
    std::vector<std::string> examples;
};

struct GenreInfo {
    std::string name;
    std::map<std::string, ProgressionInfo> progressions;
};

// Convert Roman numeral notation to scale degree
inline uint8_t roman_to_degree(const std::string& roman) {
    std::string upper = roman;
    // Convert to uppercase for lookup
    for (char& c : upper) {
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    }
    
    if (upper == "I") return 1;
    if (upper == "II") return 2;
    if (upper == "III") return 3;
    if (upper == "IV") return 4;
    if (upper == "V") return 5;
    if (upper == "VI") return 6;
    if (upper == "VII") return 7;
    if (upper == "BVII") return 7; // bVII -> VII
    if (upper == "BVI") return 6;  // bVI -> VI
    if (upper == "BIII") return 3; // bIII -> III
    if (upper == "BII") return 2;  // bII -> II
    if (upper == "BV") return 5;   // bV -> V
    return 1; // Default to I
}

// Convert chord quality names from Python format to simpler format
inline std::string convert_quality(const std::string& quality) {
    if (quality == "major") return "maj";
    if (quality == "minor") return "min";
    if (quality == "maj7") return "maj7";
    if (quality == "min7") return "min7";
    if (quality == "dom7") return "dom7";
    if (quality == "sus2") return "sus2";
    if (quality == "sus4") return "sus4";
    if (quality == "dim") return "dim";
    if (quality == "aug") return "aug";
    if (quality == "min6") return "min6";
    if (quality == "6") return "6";
    if (quality == "maj9") return "maj9";
    if (quality == "min9") return "min9";
    if (quality == "dom9") return "dom9";
    if (quality == "maj11") return "maj11";
    if (quality == "min11") return "min11";
    if (quality == "maj13") return "maj13";
    if (quality == "min13") return "min13";
    if (quality == "dom13") return "dom13";
    if (quality == "half-dim7") return "min7b5";
    if (quality == "min7b5") return "min7b5";
    if (quality == "dom7#9") return "dom7";
    if (quality == "dom7b9") return "dom7";
    if (quality == "maj7#11") return "maj7";
    if (quality == "dom7#5") return "aug7";
    if (quality == "dom7#11") return "dom7";
    if (quality == "add9") return "add9";
    return "maj"; // Default
}

// Create progression step from roman numeral and quality
inline ProgressionStep make_step(const std::string& roman, const std::string& quality) {
    ProgressionStep step;
    step.scale_degree = roman_to_degree(roman);
    step.chord_quality = convert_quality(quality);
    step.octave = 0;
    step.velocity = 100;
    step.bend_into_next = false;
    step.inversion = 0;
    step.transpose = 0;
    return step;
}

// All chord progressions imported from orchid-pi
static const std::map<std::string, GenreInfo> PROGRESSIONS_DATABASE = {
    {"pop", {
        "Pop / Rock",
        {
            {"I-V-vi-IV", {
                "Pop Hits (Axis of Awesome)",
                "The most popular pop progression. Used in thousands of hits.",
                {
                    make_step("I", "major"),
                    make_step("V", "major"),
                    make_step("vi", "minor"),
                    make_step("IV", "major")
                },
                {"Let It Be", "With or Without You", "No Woman No Cry"}
            }},
            {"I-IV-V-I", {
                "Classic Rock",
                "The foundation of rock music. Simple and powerful.",
                {
                    make_step("I", "major"),
                    make_step("IV", "major"),
                    make_step("V", "major"),
                    make_step("I", "major")
                },
                {"Louie Louie", "La Bamba", "Wild Thing"}
            }},
            {"vi-IV-I-V", {
                "Sensitive/Emo",
                "Emotional and melancholic. Common in ballads and emo.",
                {
                    make_step("vi", "minor"),
                    make_step("IV", "major"),
                    make_step("I", "major"),
                    make_step("V", "major")
                },
                {"Grenade", "Apologize"}
            }},
            {"I-vi-IV-V", {
                "50s Progression (Doo-Wop)",
                "Classic 1950s progression. Nostalgic sound.",
                {
                    make_step("I", "major"),
                    make_step("vi", "minor"),
                    make_step("IV", "major"),
                    make_step("V", "major")
                },
                {"Stand By Me", "Every Breath You Take"}
            }}
        }
    }},
    {"jazz", {
        "Jazz",
        {
            {"ii-V-I", {
                "Jazz Turnaround",
                "The most important progression in jazz. Foundation of improvisation.",
                {
                    make_step("ii", "min7"),
                    make_step("V", "dom7"),
                    make_step("I", "maj7")
                },
                {"Countless jazz standards"}
            }},
            {"I-vi-ii-V", {
                "Rhythm Changes (A section)",
                "From 'I Got Rhythm'. Very common in bebop.",
                {
                    make_step("I", "maj7"),
                    make_step("vi", "min7"),
                    make_step("ii", "min7"),
                    make_step("V", "dom7")
                },
                {"I Got Rhythm", "Anthropology"}
            }},
            {"metheny-bright", {
                "Metheny Bright Sound",
                "Lush major extensions, Pat Metheny signature.",
                {
                    make_step("I", "maj9"),
                    make_step("II", "maj9"),
                    make_step("iii", "min11"),
                    make_step("vi", "min9"),
                    make_step("ii", "min11"),
                    make_step("V", "dom9"),
                    make_step("I", "maj13")
                },
                {"Phase Dance", "James"}
            }},
            {"jazz-251-extended", {
                "II-V-I Extended",
                "Classic jazz with extensions.",
                {
                    make_step("ii", "min9"),
                    make_step("V", "dom13"),
                    make_step("I", "maj9"),
                    make_step("vi", "min11")
                },
                {"Standard jazz progression"}
            }}
        }
    }},
    {"blues", {
        "Blues",
        {
            {"12-bar-blues", {
                "12-Bar Blues",
                "The classic 12-bar blues progression.",
                {
                    make_step("I", "dom7"), make_step("I", "dom7"), make_step("I", "dom7"), make_step("I", "dom7"),
                    make_step("IV", "dom7"), make_step("IV", "dom7"),
                    make_step("I", "dom7"), make_step("I", "dom7"),
                    make_step("V", "dom7"), make_step("IV", "dom7"),
                    make_step("I", "dom7"), make_step("V", "dom7")
                },
                {"Sweet Home Chicago", "The Thrill Is Gone"}
            }},
            {"minor-blues", {
                "Minor Blues",
                "12-bar blues in minor key.",
                {
                    make_step("i", "min7"), make_step("i", "min7"), make_step("i", "min7"), make_step("i", "min7"),
                    make_step("iv", "min7"), make_step("iv", "min7"),
                    make_step("i", "min7"), make_step("i", "min7"),
                    make_step("v", "min7"), make_step("iv", "min7"),
                    make_step("i", "min7"), make_step("v", "min7")
                },
                {"Black Magic Woman"}
            }}
        }
    }},
    {"edm", {
        "Electronic / EDM",
        {
            {"i-bVII-bVI-bVII", {
                "EDM Minor",
                "Dark and driving. Common in progressive house.",
                {
                    make_step("i", "minor"),
                    make_step("bVII", "major"),
                    make_step("bVI", "major"),
                    make_step("bVII", "major")
                },
                {"Many progressive house tracks"}
            }},
            {"I-bVII-IV", {
                "Mixolydian EDM",
                "Bright and energetic. Festival anthems.",
                {
                    make_step("I", "major"),
                    make_step("bVII", "major"),
                    make_step("IV", "major")
                },
                {"Swedish House Mafia style"}
            }}
        }
    }},
    {"rnb", {
        "R&B / Soul",
        {
            {"I-iii-IV-iv", {
                "Soul Progression",
                "Smooth R&B with chromatic iv chord.",
                {
                    make_step("I", "maj7"),
                    make_step("iii", "min7"),
                    make_step("IV", "maj7"),
                    make_step("iv", "min7")
                },
                {"Isn't She Lovely"}
            }},
            {"rnb-contemporary", {
                "Contemporary R&B",
                "Modern R&B with jazz chords.",
                {
                    make_step("I", "maj13"),
                    make_step("vi", "min11"),
                    make_step("IV", "maj9"),
                    make_step("iii", "min9"),
                    make_step("ii", "min11"),
                    make_step("V", "dom13")
                },
                {"The Weeknd", "SZA"}
            }}
        }
    }},
    {"latin", {
        "Latin / Bossa Nova",
        {
            {"iim7-V7-Imaj7", {
                "Bossa Nova",
                "Classic bossa nova turnaround.",
                {
                    make_step("ii", "min7"),
                    make_step("V", "dom7"),
                    make_step("I", "maj7"),
                    make_step("I", "maj7")
                },
                {"Girl From Ipanema"}
            }},
            {"Im-IVm-V7-Im", {
                "Andalusian Cadence",
                "Spanish/Flamenco sound.",
                {
                    make_step("i", "minor"),
                    make_step("bVII", "major"),
                    make_step("bVI", "major"),
                    make_step("V", "major")
                },
                {"Hit The Road Jack"}
            }}
        }
    }},
    {"ambient", {
        "Ambient / Atmospheric",
        {
            {"I-bVII-bVI", {
                "Ambient Descending",
                "Dreamy descending progression.",
                {
                    make_step("I", "maj7"),
                    make_step("bVII", "maj7"),
                    make_step("bVI", "maj7")
                },
                {"Brian Eno style"}
            }},
            {"Im-bVImaj7-bIIImaj7-bVIImaj7", {
                "Minor Ambient",
                "Dark and atmospheric.",
                {
                    make_step("i", "min9"),
                    make_step("bVI", "maj9"),
                    make_step("bIII", "maj9"),
                    make_step("bVII", "maj7")
                },
                {"Post-rock", "ambient"}
            }}
        }
    }},
    {"indie", {
        "Indie / Alternative",
        {
            {"I-III-vi-IV", {
                "Indie Rock",
                "Alternative progression with major III.",
                {
                    make_step("I", "major"),
                    make_step("III", "major"),
                    make_step("vi", "minor"),
                    make_step("IV", "major")
                },
                {"Indie bands"}
            }},
            {"vi-V-IV-V", {
                "Sad Indie",
                "Melancholic indie progression.",
                {
                    make_step("vi", "minor"),
                    make_step("V", "major"),
                    make_step("IV", "major"),
                    make_step("V", "major")
                },
                {"Radiohead style"}
            }}
        }
    }},
    {"gospel", {
        "Gospel / Christian",
        {
            {"I-IV-I-V-IV-I", {
                "Gospel Progression",
                "Uplifting gospel sound.",
                {
                    make_step("I", "maj7"),
                    make_step("IV", "maj7"),
                    make_step("I", "maj7"),
                    make_step("V", "dom7"),
                    make_step("IV", "maj7"),
                    make_step("I", "maj7")
                },
                {"Gospel music"}
            }},
            {"gospel-251", {
                "Gospel II-V-I",
                "Classic gospel with passing chords.",
                {
                    make_step("ii", "min9"),
                    make_step("V", "dom13"),
                    make_step("I", "maj9"),
                    make_step("vi", "min7")
                },
                {"Traditional gospel sound"}
            }}
        }
    }},
    {"hiphop", {
        "Hip-Hop / Trap",
        {
            {"i-bIII-bVII-bVI", {
                "Dark Trap",
                "Menacing trap progression.",
                {
                    make_step("i", "minor"),
                    make_step("bIII", "major"),
                    make_step("bVII", "major"),
                    make_step("bVI", "major")
                },
                {"Modern trap"}
            }},
            {"i-iv-bVII-bVI", {
                "Boom Bap",
                "Classic hip-hop sound.",
                {
                    make_step("i", "min7"),
                    make_step("iv", "min7"),
                    make_step("bVII", "dom7"),
                    make_step("bVI", "maj7")
                },
                {"90s hip-hop"}
            }}
        }
    }},
    {"famous_songs", {
        "Famous Songs",
        {
            {"let-it-be", {
                "Let It Be (Beatles)",
                "One of the most iconic progressions ever.",
                {
                    make_step("I", "major"),
                    make_step("V", "major"),
                    make_step("vi", "minor"),
                    make_step("IV", "major")
                },
                {"Let It Be", "No Woman No Cry", "With or Without You"}
            }},
            {"hotel-california", {
                "Hotel California (Eagles)",
                "Minor key classic rock.",
                {
                    make_step("i", "minor"),
                    make_step("V", "major"),
                    make_step("bVII", "major"),
                    make_step("IV", "major"),
                    make_step("bVI", "major"),
                    make_step("bIII", "major"),
                    make_step("bVII", "major"),
                    make_step("i", "minor")
                },
                {"Hotel California"}
            }},
            {"creep", {
                "Creep (Radiohead)",
                "Grunge/alternative classic.",
                {
                    make_step("I", "major"),
                    make_step("III", "major"),
                    make_step("IV", "major"),
                    make_step("iv", "minor")
                },
                {"Creep", "Air That I Breathe"}
            }}
        }
    }},
    {"country", {
        "Country",
        {
            {"country-classic", {
                "Country Classic",
                "Traditional country sound.",
                {
                    make_step("I", "major"),
                    make_step("IV", "major"),
                    make_step("I", "major"),
                    make_step("V", "major")
                },
                {"Ring of Fire", "Folsom Prison Blues"}
            }},
            {"country-modern", {
                "Modern Country Pop",
                "Contemporary country-pop hybrid.",
                {
                    make_step("I", "major"),
                    make_step("V", "major"),
                    make_step("vi", "minor"),
                    make_step("IV", "major")
                },
                {"Need You Now", "Cruise"}
            }}
        }
    }},
    {"reggae", {
        "Reggae / Ska",
        {
            {"reggae-classic", {
                "Reggae Classic",
                "Classic reggae offbeat.",
                {
                    make_step("I", "major"),
                    make_step("IV", "major"),
                    make_step("I", "major"),
                    make_step("V", "major")
                },
                {"Three Little Birds", "One Love"}
            }},
            {"reggae-minor", {
                "Minor Reggae",
                "Minor key reggae vibes.",
                {
                    make_step("i", "minor"),
                    make_step("bVII", "major"),
                    make_step("bVI", "major"),
                    make_step("V", "major")
                },
                {"Redemption Song"}
            }}
        }
    }},
    {"funk", {
        "Funk / Soul",
        {
            {"funk-vamp", {
                "Funk Vamp",
                "Groove-based funk loop.",
                {
                    make_step("i", "min7"),
                    make_step("IV", "dom7")
                },
                {"Superstition", "Cissy Strut"}
            }},
            {"funk-extended", {
                "Extended Funk",
                "Complex funk with passing chords.",
                {
                    make_step("i", "min9"),
                    make_step("iv", "min9"),
                    make_step("i", "min9"),
                    make_step("bVII", "dom9")
                },
                {"Chameleon", "Cantaloupe Island"}
            }}
        }
    }},
    {"metal", {
        "Metal",
        {
            {"metal-power", {
                "Power Metal",
                "Epic power metal progression.",
                {
                    make_step("i", "minor"),
                    make_step("bVI", "major"),
                    make_step("bVII", "major"),
                    make_step("i", "minor")
                },
                {"Thrash/Power metal"}
            }},
            {"metal-doom", {
                "Doom Metal",
                "Heavy, crushing progression.",
                {
                    make_step("i", "minor"),
                    make_step("bVI", "major"),
                    make_step("bIII", "major"),
                    make_step("bVII", "major")
                },
                {"Black Sabbath style"}
            }}
        }
    }},
    {"flamenco", {
        "Flamenco",
        {
            {"flamenco-phrygian", {
                "Flamenco Phrygian",
                "Classic flamenco Phrygian cadence.",
                {
                    make_step("i", "minor"),
                    make_step("bVII", "major"),
                    make_step("bVI", "major"),
                    make_step("V", "major")
                },
                {"Traditional flamenco", "Paco de Lucía"}
            }},
            {"flamenco-rumba", {
                "Rumba Flamenca",
                "Rumba flamenco fusion.",
                {
                    make_step("i", "min7"),
                    make_step("bVII", "dom7"),
                    make_step("bVI", "maj7"),
                    make_step("V", "dom7")
                },
                {"Gipsy Kings style"}
            }}
        }
    }},
    {"celtic", {
        "Celtic",
        {
            {"irish-jig", {
                "Irish Jig",
                "Traditional Irish jig.",
                {
                    make_step("I", "major"),
                    make_step("IV", "major"),
                    make_step("I", "major"),
                    make_step("V", "major")
                },
                {"Traditional Irish dance"}
            }},
            {"celtic-modal", {
                "Celtic Modal",
                "Dorian mode Celtic sound.",
                {
                    make_step("i", "min7"),
                    make_step("bVII", "maj7"),
                    make_step("i", "min7"),
                    make_step("IV", "maj7")
                },
                {"Enya", "Clannad"}
            }}
        }
    }}
};

} // namespace engine