#include <unordered_map>
#include <regex>

#include "ExtractCaptions.h"

namespace {

// Define the translations map
const std::unordered_map<std::string, std::unordered_map<std::string, std::string>> translations = {
    {"fr", {{"Figure", "Figure|FIGURE|FIG\\.?|Fig\\.?"}, {"Table", "Tableau|TABLEAU"}}},
    {"es", {{"Figure", "Figura|FIGURA|FIG\\.?|Fig\\.?"}, {"Table", "Tabla|TABLA"}}},
    {"it", {{"Figure", "Figura|FIGURA|FIG\\.?|Fig\\.?"}, {"Table", "Tabella|TABELLA"}}},
    {"de", {{"Figure", "Abbildung|ABBILDUNG|Figur|FIGUR|FIG\\.?|Fig\\.?|ABB\\.?|Abb\\.?"}, {"Table", "Tabelle|TABELLE"}}},
    {"pt", {{"Figure", "Figura|FIGURA|FIG\\.?|Fig\\.?"}, {"Table", "Tabela|TABELA"}}},
    {"nl", {{"Figure", "Figuur|FIGUUR|FIG\\.?|Fig\\.?"}, {"Table", "Tabel|TABEL"}}},
    {"da", {{"Figure", "Figur|FIGUR|FIG\\.?|Fig\\.?"}, {"Table", "Tabel|TABEL"}}},
    {"sv", {{"Figure", "Figur|FIGUR|FIG\\.?|Fig\\.?"}, {"Table", "Tabell|TABELL"}}},
    {"no", {{"Figure", "Figur|FIGUR|FIG\\.?|Fig\\.?"}, {"Table", "Tabell|TABELL"}}}
};

// ****** Gathing caption candidates *******
// TODO using an int as the FigureId is kind of a hack
typedef int FigureId;

class CaptionCandidate {
public:
  CaptionCandidate()
      : word(NULL), lineStart(false), blockStart(false), type(FIGURE),
        number(-1), page(-1), periodMatch(false), colonMatch(false),
        caps(false), abbreviated(false) {}

  CaptionCandidate(const TextWord *word, bool lineStart, bool blockStart,
                   FigureType type, int number, int page, bool periodMatch,
                   bool colonMatch, bool caps, bool abbreviated)
      : word(word), lineStart(lineStart), blockStart(blockStart), type(type),
        number(number), page(page), periodMatch(periodMatch),
        colonMatch(colonMatch), caps(caps), abbreviated(abbreviated) {}

  FigureId getId() { return number * (type == FIGURE ? 1 : -1); }

  const TextWord *word;
  bool lineStart;
  bool blockStart;
  FigureType type;
  int number;
  int page;
  bool periodMatch;
  bool colonMatch;
  bool caps;
  bool abbreviated;
};


int romanToInt(const std::string& s) {
    std::map<char, int> roman = {
        {'I', 1},
        {'V', 5},
        {'X', 10}
    };
    int total = 0;
    int prevValue = 0;
    for (int i = s.length() - 1; i >= 0; i--) {
        int value = roman[s[i]];
        if (value < prevValue)
            total -= value;
        else
            total += value;
        prevValue = value;
    }
    return total;
}

CaptionCandidate constructCandidate(const TextWord *word, int page, bool lineStart,
                                    bool blockStart, bool tablesOnly, const std::string& lang = "en") {
  if (word->getNext() == NULL)
    return CaptionCandidate();

  std::string figureTerm = "Figure|FIGURE|FIG\\.?|Fig\\.?";
  std::string tableTerm = "Table|TABLE";

  if (translations.find(lang) != translations.end()) {
    figureTerm = translations.at(lang).at("Figure");
    tableTerm = translations.at(lang).at("Table");
  }
    
//   const std::string captionCue = tablesOnly ? "^(Table|TABLE)$" : "^(Figure|FIGURE|FIG\\.?|Fig\\.?)$";
  const std::string captionCue = tablesOnly ? "^(" + tableTerm + ")$" : "^(" + figureTerm + ")$";
  
  const std::regex wordRegex = std::regex(captionCue);

  std::match_results<const char *> wordMatch;
  if (not std::regex_match(word->getText()->c_str(), wordMatch, wordRegex))
    return CaptionCandidate();

  const std::regex numberRegex = std::regex("^([1-9][.\\-]|[A-H][.\\-]?)?([0-9IVX]+)(:|\\.)?$");

  std::match_results<const char *> numberMatch;
  std::regex_match(word->getNext()->getText()->c_str(), numberMatch,
                   numberRegex);

  int number;
  std::string captionNumStr;
  std::string captionIntStr;
  if (not numberMatch.empty()) {
    captionNumStr = numberMatch[0].str(); // the whole caption number match including prefix and separator
    captionIntStr = numberMatch[2].str(); // just the integer part of the caption, e.g. 1
    try {
      number = std::stoi(captionIntStr);
    } catch (std::invalid_argument e) {
      number = romanToInt(captionIntStr);
    }
    
  } else {
    return CaptionCandidate();
  }
  bool periodMatch = false, colonMatch = false;
  if (captionNumStr.at(captionNumStr.length() - 1) == ':') {
    colonMatch = true;
  } else if (captionNumStr.at(captionNumStr.length() - 1) == '.') {
    periodMatch = true;
  }
  FigureType type = wordMatch[0].str().at(0) == 'T' ? TABLE : FIGURE;
  return CaptionCandidate(word, lineStart, blockStart, type, number, page,
                          periodMatch, colonMatch, wordMatch[2].length() > 0,
                          wordMatch[3].length() > 0);
}

// Maps ids -> all candidates that have that id
typedef std::unordered_map<FigureId,
                           std::unique_ptr<std::vector<CaptionCandidate>>>
    CandidateCollection;

CandidateCollection collectCandidates(const std::vector<TextPage *> &pages, bool tablesOnly, const std::string& lang = "en") {
  CandidateCollection collection = CandidateCollection();
  for (size_t i = 0; i < pages.size(); ++i) {
    TextFlow const *flow = pages.at(i)->getFlows();
    while (flow != NULL) {
      const TextBlock *block = flow->getBlocks();
      while (block != NULL) {
        bool blockStart = true;
        const TextLine *line = block->getLines();
        while (line != NULL) {
          const TextWord *word = line->getWords();
          bool lineStart = true;
          while (word != NULL) {
            CaptionCandidate cc =
                constructCandidate(word, i, lineStart, blockStart, tablesOnly, lang);
            if (cc.word != NULL) {
              int id = cc.getId();
              if (collection.find(id) == collection.end()) {
                collection[id] = std::unique_ptr<std::vector<CaptionCandidate>>(
                    new std::vector<CaptionCandidate>());
              }
              collection[cc.getId()]->push_back(cc);
            }
            word = word->getNext();
            lineStart = false;
          }
          line = line->getNext();
          blockStart = false;
        }
        block = block->getNext();
      }
      flow = flow->getNext();
    }
  }
  return collection;
}

// ****** CaptionCandidate Filteres ******

class CandidateFilter {
public:
  CandidateFilter(const char *name, const bool asGroup)
      : name(name), asGroup(asGroup) {}

  const char *name;
  const bool asGroup;
  virtual bool check(const CaptionCandidate &cc) = 0;
};

class ColonOnly : public CandidateFilter {
public:
  ColonOnly() : CandidateFilter("Colon Only", true) {}
  bool check(const CaptionCandidate &cc) { return cc.colonMatch; }
};

class PeriodOnly : public CandidateFilter {
public:
  PeriodOnly() : CandidateFilter("Period Only", true) {}
  bool check(const CaptionCandidate &cc) { return cc.periodMatch; }
};

class AbbrevFiguresOnly : public CandidateFilter {
public:
  AbbrevFiguresOnly() : CandidateFilter("Only Abbreviated Figures", true) {}
  bool check(const CaptionCandidate &cc) {
    return cc.type == TABLE || cc.abbreviated;
  }
};

class AllCapsFiguresOnly : public CandidateFilter {
public:
  AllCapsFiguresOnly() : CandidateFilter("Only All Caps Figures", true) {}
  bool check(const CaptionCandidate &cc) { return cc.type == TABLE || cc.caps; }
};

class BlockStartOnly : public CandidateFilter {
public:
  BlockStartOnly() : CandidateFilter("Block Start Only", false) {}
  bool check(const CaptionCandidate &cc) { return cc.blockStart; }
};

class LineStartOnly : public CandidateFilter {
public:
  LineStartOnly() : CandidateFilter("Line Start Only", false) {}
  bool check(const CaptionCandidate &cc) { return cc.lineStart; }
};

class BoldOnly : public CandidateFilter {
public:
  BoldOnly() : CandidateFilter("Bold Only", true) {}
  bool check(const CaptionCandidate &cc) { return wordIsBold(cc.word); }
};

class ItalicOnly : public CandidateFilter {
public:
  ItalicOnly() : CandidateFilter("Italic Only", true) {}
  bool check(const CaptionCandidate &cc) { return wordIsItalic(cc.word); }
};

class NextWordOnly : public CandidateFilter {
public:
  NextWordOnly() : CandidateFilter("Next Word Only", false) {}
  bool check(const CaptionCandidate &cc) {
    return cc.word->getNext()->getNext() != NULL;
  }
};

class NoNextWord : public CandidateFilter {
public:
  NoNextWord() : CandidateFilter("No Next Word", true) {}
  bool check(const CaptionCandidate &cc) {
    return cc.word->getNext()->getNext() == NULL;
  }
};

// ********** Applying the filters to get the final results ******

// TODO we should be a bit more clever and accept filter that remove
// caption reference if it is the only way to remove all duplicates
bool applyFilter(CandidateFilter *filter, CandidateCollection &collection) {
  bool removedAnything = false;
  for (auto &ccs : collection) {
    size_t filterCount = 0;
    for (CaptionCandidate cc : *ccs.second.get()) {
      if (filter->check(cc))
        filterCount += 1;
    }
    if (filterCount == 0 and filter->asGroup) {
      return false;
    } else if (filterCount < ccs.second->size() and filterCount >= 1) {
      removedAnything = true;
    }
  }
  if (not removedAnything)
    return false;
  for (auto &ccs : collection) {
    for (size_t i = 0; i < ccs.second.get()->size(); ++i) {
      if (ccs.second->size() > 1 and not filter->check(ccs.second->at(i))) {
        ccs.second->erase(ccs.second->begin() + i);
        --i;
      }
    }
  }
  return true;
}

bool anyDuplicates(const CandidateCollection &collection) {
  for (auto &ccs : collection) {
    if (ccs.second.get()->size() > 1) {
      return true;
    }
  }
  return false;
}

} // End namespace

std::map<int, std::vector<CaptionStart>>
extractCaptionsFromText(const std::vector<TextPage *> &textPages,
                        bool verbose,
                        bool tablesOnly,
                        const std::string& lang = "en") {
  CandidateCollection candidates = collectCandidates(textPages, tablesOnly, lang);
  // In order to be considered
  ColonOnly f1 = ColonOnly();
  PeriodOnly f2 = PeriodOnly();
  BoldOnly f3 = BoldOnly();
  ItalicOnly f4 = ItalicOnly();
  AllCapsFiguresOnly f5 = AllCapsFiguresOnly();
  AbbrevFiguresOnly f6 = AbbrevFiguresOnly();
  NoNextWord f7 = NoNextWord();
  BlockStartOnly f8 = BlockStartOnly();
  LineStartOnly f9 = LineStartOnly();
  NextWordOnly f10 = NextWordOnly();
  std::vector<CandidateFilter *> filters = {&f1, &f2, &f3, &f4, &f5,
                                            &f6, &f7, &f8, &f9, &f10};

  if (verbose) {
    printf("Scanning for captions...\n");
    int nCandidates = 0;
    for (auto &ccs : candidates) {
      nCandidates += ccs.second->size();
    }
    printf("Collected %d candidates for %d detected captions\n", nCandidates,
           (int)candidates.size());
  }

  bool triedAll = false;
  while (anyDuplicates(candidates) and not triedAll) {
    triedAll = true;
    for (CandidateFilter *cf : filters) {
      if (applyFilter(cf, candidates)) {
        if (verbose) {
          int nCandidates = 0;
          for (auto &ccs : candidates) {
            nCandidates += ccs.second->size();
          }
          printf("Applied filter %s (%d remain)\n", cf->name, nCandidates);
        }
        triedAll = false;
        break;
      }
    }
  }

  // Check for non consecutive figures / tables, add any found as errors
  if (verbose) {
    int maxTable = 0;
    int maxFigure = 0;
    int nTables = 0;
    int nFigures = 0;
    for (auto &ccs : candidates) {
      if (ccs.first > 0) {
        nFigures++;
        maxFigure = std::max(ccs.first, maxFigure);
      } else {
        nTables++;
        maxTable = std::max(-ccs.first, maxTable);
      }
    }
    if (maxTable != nTables) {
      printf("Warning: Max table number found was %d, but only found %d table "
             "captions!\n",
             maxTable, nTables);
    }
    if (maxFigure != nFigures) {
      printf(
          "Warning: Max figure number found was %d, but only found %d figure "
          "captions!\n",
          maxFigure, nFigures);
    }
  }

  std::map<int, std::vector<CaptionStart>> output =
      std::map<int, std::vector<CaptionStart>>();

  // Add in all the captions
  for (CandidateCollection::iterator it = candidates.begin();
       it != candidates.end();) {
    std::vector<CaptionCandidate> *captionOptions = it->second.get();
    if (captionOptions->size() <= 2) {
      if (verbose && captionOptions->size() == 2) {
        // This might be due to a continued Figure, but even if it is a mistake
        // we can hope the following steps will not find any figure regions
        // for the incorrect candidate we selected
        printf("Two candidates for %s%d, keeping both\n",
               getFigureTypeString(captionOptions->at(0).type),
               captionOptions->at(0).number);
      }
      for (CaptionCandidate cc : *captionOptions) {
        output[cc.page].push_back(
            CaptionStart(cc.page, cc.number, cc.word, cc.type));
      }
    } else if (verbose && captionOptions->size() > 0) {
      printf("%d candidates for %s%d, excluding them\n",
             (int)captionOptions->size(),
             getFigureTypeString(captionOptions->at(0).type),
             captionOptions->at(0).number);
    }
    candidates.erase(it++);
  }

  if (verbose)
    printf("Done parsing captions.\n\n");
  return output;
}
