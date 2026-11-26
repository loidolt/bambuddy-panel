// The settings page the panel serves during setup (setup_web.cpp), as text. No web server and
// no Arduino String in here, so the simulator can write the same page to a file to look at.
#pragma once

// Where the page's text goes: an Arduino String on the panel, a std::string in the simulator.
struct PageText {
  virtual PageText &operator+=(const char *text) = 0;
  PageText &operator+=(int number);
  virtual ~PageText() {}
};

// A printer the API key may see, offered by name.
struct PageChoice {
  int id;
  char name[32];
};

struct PageData {
  bool locked = false;
  const char *msg = "";       // shown in a box above the form; HTML, already escaped
  const char *kind = "";      // "" plain, "e" error, "ok" success
  const char *typedCode = ""; // the code as typed, so a failed Save doesn't ask for it again
  const char *url = "";       // the saved Bambuddy address
  bool keySaved = false;      // the key itself is never put in the page
  int printerId = 0;
  const PageChoice *choices = nullptr;
  int choiceCount = 0;
  int ticket = 0;          // binds offline confirmation to the reviewed draft
  bool finishAnyway = false;  // offers to keep settings Bambuddy didn't accept (it may just be down)
  const char *printerName = ""; // the printer this panel shows, when known: several panels look alike
  bool done = false;          // nothing more to fill in: the message alone
  const char *version = "";   // the firmware's version, at the foot of the page; "" leaves it out
  const char *board = "";     // this panel's build, e.g. "cyd-st7789": offers the firmware update; "" doesn't
};

void setup_page(PageText &p, const PageData &d);
// Adds text with the characters that mean something in HTML replaced.
void page_escape(PageText &p, const char *text);
