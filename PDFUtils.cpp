#include <algorithm>
#include <stdexcept>

#include <PDFDoc.h>
#include <SplashOutputDev.h>
#include <splash/SplashBitmap.h>
#include <Page.h>

#include "PDFUtils.h"

CaptionStart::CaptionStart(int page, int number, const TextWord *word,
                           FigureType type)
    : page(page), type(type), number(number), word(word) {}

Caption::Caption(CaptionStart captionStart, BOX *boundingBox)
    : page(captionStart.page), number(captionStart.number),
      type(captionStart.type), boundingBox(boxCopy(boundingBox)) {}

Caption::Caption(int page, int number, FigureType type, BOX *boundingBox)
    : page(page), number(number), type(type),
      boundingBox(boxCopy(boundingBox)) {}

Figure::Figure(Caption caption, BOX *imageBB)
    : type(caption.type), page(caption.page), number(caption.number),
      imageBB(imageBB), captionBB(caption.boundingBox) {}

Figure::Figure(CaptionStart captionStart)
    : type(captionStart.type), page(captionStart.page),
      number(captionStart.number), imageBB(NULL), captionBB(NULL) {}

void displayBox(PIX *pix, BOX *box) {
  BOXA *tmp = boxaCreate(1);
  boxaAddBox(tmp, box, L_CLONE);
  PIX *dis = pixDrawBoxa(pix, tmp, 2, 0xff000000);
  pixDisplay(dis, 0, 0);
  boxaDestroy(&tmp);
  pixDestroy(&dis);
}

const char *getFigureTypeString(FigureType type) {
  switch (type) {
  case FIGURE:
    return "Figure";
  case TABLE:
    return "Table";
  default:
    throw new std::invalid_argument("Unkown FigureType");
  }
}

class ImageDetectDev : public OutputDev {
public:
  ImageDetectDev(int maxHeight, int maxWidth)
      : maxHeight(maxHeight), maxWidth(maxWidth), filled(false) {}

  bool upsideDown() { return false; }
  bool useDrawChar() { return false; }
  bool interpretType3Chars() { return true; }

  virtual void drawImageMask(GfxState *state, Object *ref, Stream *str,
                             int width, int height, bool invert,
                             bool interpolate, bool inlineImg) {
    if (width > maxWidth and height > maxHeight)
      filled = true;
  }

  virtual void drawImage(GfxState *state, Object *ref, Stream *str, int width,
                         int height, GfxImageColorMap *colorMap,
                         bool interpolate, const int *maskColors, bool inlineImg) {
    if (width > maxWidth and height > maxHeight)
      filled = true;
  }

  virtual void drawMaskedImage(GfxState *state, Object *ref, Stream *str,
                               int width, int height,
                               GfxImageColorMap *colorMap, bool interpolate,
                               Stream *maskStr, int maskWidth, int maskHeight,
                               bool maskInvert, bool maskInterpolate) {
    if (width > maxWidth and height > maxHeight)
      filled = true;
  }

  virtual void
  drawSoftMaskedImage(GfxState *state, Object *ref, Stream *str, int width,
                      int height, GfxImageColorMap *colorMap, bool interpolate,
                      Stream *maskStr, int maskWidth, int maskHeight,
                      GfxImageColorMap *maskColorMap, bool maskInterpolate) {
    if (width > maxWidth and height > maxHeight)
      filled = true;
  }

  bool getFilled() { return filled; }

private:
  int maxHeight;
  int maxWidth;
  bool filled;
};

// OutputDevice that ignores characters
class SplashGraphicsOutputDev : public SplashOutputDev {

public:
  SplashGraphicsOutputDev(SplashColorMode colorModeA, int bitmapRowPadA,
                          bool reverseVideoA, SplashColorPtr paperColorA)
      : SplashOutputDev(colorModeA, bitmapRowPadA, reverseVideoA, paperColorA) {}

  bool useDrawChar() override { return true; }

  bool interpretType3Chars() override { return false; }

  void type3D1(GfxState *state, double wx, double wy, double llx, double lly,
               double urx, double ury) override {}

  void beginStringOp(GfxState *state) override {}

  void endStringOp(GfxState *state) override {}

  void beginString(GfxState *state, const GooString *str) override {}

  void endString(GfxState *state) override {}

  void drawChar(GfxState *state, double x, double y, double dx, double dy,
                double originX, double originY, CharCode code, int nBytes,
                const Unicode *u, int uLen) override {}

  void drawString(GfxState *state, const GooString *str) override {}

  bool beginType3Char(GfxState *state, double x, double y, double dx,
                       double dy, CharCode code, const Unicode *u, int uLen) override {
    // TODO decide if true is correct
    return false;
  }

  void endType3Char(GfxState *state) override {}

  void beginTextObject(GfxState *state) override {}

  void endTextObject(GfxState *state) override {}

  void incCharCount(int nChars) override {}

  void beginActualText(GfxState *state, const GooString *text) override {}

  void endActualText(GfxState *state) override {}
};

PIX *bitmapToPix(SplashBitmap *bitmap) {
  if (bitmap->getMode() != splashModeMono8)
    return NULL;
  PIX *pix = pixCreate(bitmap->getWidth(), bitmap->getHeight(), 8);
  SplashColorPtr pixel = new unsigned char[1];
  for (int x = 0; x < bitmap->getWidth(); ++x) {
    for (int y = 0; y < bitmap->getHeight(); ++y) {
      bitmap->getPixel(x, y, pixel);
      pixSetPixel(pix, x, y, pixel[0]);
    }
  }
  return pix;
}

PIX *fullColorBitmapToPix(SplashBitmap *bitmap) {
  if (bitmap->getMode() != splashModeRGB8)
    return NULL;
  PIX *pix = pixCreate(bitmap->getWidth(), bitmap->getHeight(), 32);
  SplashColorPtr pixel = new unsigned char[3];
  for (int x = 0; x < bitmap->getWidth(); ++x) {
    for (int y = 0; y < bitmap->getHeight(); ++y) {
      bitmap->getPixel(x, y, pixel);
      pixSetPixel(pix, x, y, 
        ((pixel[0] & 0xff) << 24) + ((pixel[1] & 0xff) << 16) +
        ((pixel[2] & 0xff) << 8) + bitmap->getAlpha(x, y));
    }
  }
  return pix;
}

bool isFilledByImage(PDFDoc *doc, int page) {
  int dpi = 72;
  ImageDetectDev *dev = new ImageDetectDev(doc->getPageMediaWidth(page) - 10,
                                           doc->getPageMediaHeight(page) - 10);
  doc->displayPage(dev, page, dpi, dpi, 0, true, false, false);
  bool filled = dev->getFilled();
  delete dev;
  return filled;
}

PIX *getPix(SplashOutputDev *splashOut, PDFDoc *doc, int page, double dpi) {
  splashOut->startDoc(doc);
  doc->displayPage(splashOut, page, dpi, dpi, 0, true, false, false);
  return bitmapToPix(splashOut->getBitmap());
}

PIX *getFullColorPix(SplashOutputDev *splashOut, PDFDoc *doc, int page, double dpi) {
  splashOut->startDoc(doc);
  doc->displayPage(splashOut, page, dpi, dpi, 0, true, false, false);
  return fullColorBitmapToPix(splashOut->getBitmap());
}

std::unique_ptr<PIX> getFullRenderPix(PDFDoc *doc, int page, double dpi) {
  SplashColor paperColor = {255, 255, 255};
  SplashOutputDev *splashOut =
    new SplashOutputDev(splashModeMono8, 4, false, paperColor);
  std::unique_ptr<PIX> output(getPix(splashOut, doc, page, dpi));
  delete splashOut;
  return output;
}

std::unique_ptr<PIX> getGraphicOnlyPix(PDFDoc *doc, int page, double dpi) {
  SplashColor paperColor = {255, 255, 255};
  SplashGraphicsOutputDev *splashOut =
      new SplashGraphicsOutputDev(splashModeMono8, 4, false, paperColor);
  std::unique_ptr<PIX> output(getPix(splashOut, doc, page, dpi));
  delete splashOut;
  return output;
}

std::unique_ptr<PIX> getFullColorRenderPix(PDFDoc *doc, int page, double dpi) {
  SplashColor paperColor = {255, 255, 255};
  SplashOutputDev *splashOut =
    new SplashOutputDev(splashModeRGB8, 4, false, paperColor);
  std::unique_ptr<PIX> output(getFullColorPix(splashOut, doc, page, dpi));
  delete splashOut;
  return output;
}

std::vector<TextPage *> getTextPages(PDFDoc *doc, double dpi) {
  std::vector<TextPage *> text = std::vector<TextPage *>();
  for (int i = 1; i <= doc->getNumPages(); ++i) {
    // TOOD should not need to rebuild this each time
    TextOutputDev *output = new TextOutputDev(NULL, false, 0, false, false);
    doc->displayPage(output, i, dpi, dpi, 0, false, false, false);
    text.push_back(output->takeText());
    delete output;
  }
  return text;
}

PIX *drawFigureRegions(PIX *background, const std::vector<Figure> &figures) {
  BOXA *imageBoxes = boxaCreate((int)figures.size());
  BOXA *captionBoxes = boxaCreate((int)figures.size());
  BOXA *boundingBoxes = boxaCreate((int)figures.size());
  for (Figure figure : figures) {
    BOX *captionBox = figure.captionBB;
    if (captionBox != NULL)
      boxaAddBox(captionBoxes, captionBox, L_CLONE);
    BOX *imageBox = figure.imageBB;
    if (imageBox != NULL)
      boxaAddBox(imageBoxes, imageBox, L_CLONE);

    if (imageBox != NULL and captionBox != NULL) {
      BOX *boundingBox = boxBoundingRegion(captionBox, imageBox);
      int pad = 7;
      boundingBox->w += pad * 2;
      boundingBox->h += pad * 2;
      boundingBox->x -= pad;
      boundingBox->y -= pad;
      boxaAddBox(boundingBoxes, boundingBox, L_CLONE);
    }
  }
  PIX *scratch = pixCopy(NULL, background); // TODO leaks memory
  PIX *output;
  if (imageBoxes->n > 0)
    scratch = pixDrawBoxa(background, imageBoxes, 4, 0x00ff0000);
  if (captionBoxes->n > 0)
    scratch = pixDrawBoxa(scratch, captionBoxes, 4, 0x0000ff00);
  if (boundingBoxes->n > 0) {
    output = pixDrawBoxa(scratch, boundingBoxes, 4, 0xff000000);
    pixDestroy(&scratch);
  } else {
    output = scratch;
  }
  boxaDestroy(&boundingBoxes);
  boxaDestroy(&imageBoxes);
  boxaDestroy(&captionBoxes);
  return output;
}

GooString *jsonSanitizeUTF8(GooString *str) {
  int j = 0;
  str = str->copy();
  while (j < str->getLength()) {
    char c = str->getChar(j);
    if (c < 0 or c > 127) {
      // For multi-byte encoding, skip over extra trailling bytes
      int leadingOne = 2;
      while ((c >> (8 - leadingOne) & 1) == 1) {
        ++j;
        ++leadingOne;
      }
    } else if (c == '\\' or c == '\"') {
      str->insert(j, '\\');
      ++j;
    } else if (c == '\n' or c <= 31) {
      // Replace controls sequence and new lines with spaces
      str->setChar(j, ' ');
    }
    ++j;
  }
  return str;
}

void writeText(TextPage *page, BOX *bb, const char *name,
               std::ostream &output) {
  output << "\"" << name << "\" : [";
  std::unique_ptr<TextWordList> word_list = page->makeWordList(false);
  TextWordList *words = word_list.release();
  bool firstWord = true;
  for (int j = 0; j < words->getLength(); ++j) {
    TextWord *word = words->get(j);
    double x, y, x2, y2;
    word->getBBox(&x, &y, &x2, &y2);
    int contains;
    BOX wordBox = BOX{(int)(x + 0.5), (int)(y + 0.5), (int)(x2 - x + 0.5),
                      (int)(y2 - y + 0.5)};
    boxContains(bb, &wordBox, &contains);
    if (contains) {
      if (word->getText()->getLength() == 0)
        continue;
      if (not firstWord) {
        output << ",\n\t";
      } else {
        output << "\n\t";
      }
      GooString *str = jsonSanitizeUTF8(word->getText());
      output << "{\"Rotation\": " << word->getRotation() << ",\"TextBB\": [";
      output << x << "," << y << "," << x2 << "," << y2 << "], \"Text\": \"";
      output << str->c_str() << "\"}";
      delete str;
      firstWord = false;
    }
  }
  output << "\n]";
  delete words;
}

void saveFiguresImage(std::vector<Figure> &figures, PIX *original,
                      std::string prefix) {
  for (Figure fig : figures) {
    std::ostringstream ss;
    ss << std::setw(3) << std::setfill('0') << std::to_string(fig.number);
    std::string name = prefix + "-" + getFigureTypeString(fig.type) + "-" + ss.str() + ".png";
    if (fig.imageBB != NULL) {
      pixWrite(name.c_str(), pixClipRectangle(original, fig.imageBB, NULL),
               IFF_PNG);
    }
  }
}

void saveFiguresFullColorImage(std::vector<Figure> &figures, PIX *original,
                      std::string prefix, int multidpi) {
  for (Figure fig : figures) {
    std::ostringstream ss;
    ss << std::setw(3) << std::setfill('0') << std::to_string(fig.number);
    std::string name = prefix + "-" + getFigureTypeString(fig.type) + "-" + ss.str() + ".png";
    if (fig.imageBB != NULL) {

      fig.imageBB->x *= multidpi;
      fig.imageBB->y *= multidpi;
      fig.imageBB->w *= multidpi;
      fig.imageBB->h *= multidpi;

      pixWrite(name.c_str(), pixClipRectangle(original, fig.imageBB, NULL),
               IFF_PNG);

      fig.imageBB->x /= multidpi;
      fig.imageBB->y /= multidpi;
      fig.imageBB->w /= multidpi;
      fig.imageBB->h /= multidpi;
    }
  }
}

void writeFigureJSON(Figure &fig, int width, int height, double dpi,
                     std::vector<TextPage *> &text, std::ostream &output) {
  output << "{\"Type\":\"" << getFigureTypeString(fig.type) << "\",\n";
  output << "\"Number\": " << fig.number << ",\n";
  output << "\"Page\": " << (fig.page + 1) << ",\n"; // Switch from 0 indexing
  output << "\"DPI\": " << dpi << ",\n";
  output << "\"Width\": " << width << ",\n";
  output << "\"Height\": " << height << ",\n";

  TextPage *page = fig.page == -1 ? NULL : text.at(fig.page);
  if (fig.captionBB == NULL) {
    output << "\"CaptionBB\": null,\n";
    output << "\"Caption\": null,\n";
  } else {
    output << "\"CaptionBB\": [" << fig.captionBB->x << "," << fig.captionBB->y;
    output << "," << fig.captionBB->x + fig.captionBB->w << ",";
    output << fig.captionBB->y + fig.captionBB->h << "],\n";
    BOX *bb = fig.captionBB;
    GooString *caption = jsonSanitizeUTF8(
        page->getText(bb->x, bb->y, bb->x + bb->w, bb->y + bb->h, eolUnix));
    output << "\"Caption\": \"" << caption->c_str() << "\",\n";
    delete caption;
  }
  if (fig.imageBB == NULL) {
    output << "\"ImageBB\": null,\n";
    output << "\"ImageText\" : null\n";
    output << "}\n";
  } else {
    output << "\"ImageBB\": [" << fig.imageBB->x << "," << fig.imageBB->y;
    output << "," << fig.imageBB->x + fig.imageBB->w << ","
           << fig.imageBB->y + fig.imageBB->h;
    output << "],\n";
    BOX *bb = fig.imageBB;
    writeText(page, bb, "ImageText", output);
    output << "}";
  }
}
