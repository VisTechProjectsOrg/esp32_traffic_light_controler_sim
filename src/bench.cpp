#include "bench.h"
#include "state.h"
#include "signals.h"
#include "ped.h"
#include "webserver.h"
#include <config.h>

// In TB3 position order, so console shortcut 1 is strip position 1.
struct TestOutput
{
  const char *name;  // name used by /set_output
  const char *alias; // shorter name for the console
  int pin;
};

static const TestOutput outputs[] = {
    {"red", "red", LED_red_pin},
    {"yellow", "yellow", LED_yellow_pin},
    {"green", "green", LED_green_pin},
#ifdef PED_SIGNAL_ENABLED
    {"dont_walk", "hand", PED_dont_walk_pin},
    {"walk", "walk", PED_walk_pin},
    {"cd_dont_walk", "cdhand", PED_countdown_dont_walk_pin},
    {"cd_walk", "cdwalk", PED_countdown_walk_pin},
#endif
};
static const int outputCount = sizeof(outputs) / sizeof(outputs[0]);

static bool held[outputCount];
static bool flashing[outputCount];
static bool flashOn = true;
static unsigned long lastFlash = 0;

static bool lit(int i)
{
  return held[i] && (!flashing[i] || flashOn);
}

static void applyOutputs()
{
  set_traffic_light(lit(0), lit(1), lit(2));
#ifdef PED_SIGNAL_ENABLED
  set_ped_signal(lit(4), lit(3));
  set_countdown_signal(lit(6), lit(5));
#endif
}

static void clearOutputs()
{
  for (int i = 0; i < outputCount; i++)
    held[i] = flashing[i] = false;
}

// Accepts the /set_output name, the console alias, or the TB3 position number.
static int findOutput(const String &word)
{
  for (int i = 0; i < outputCount; i++)
    if (word == outputs[i].name || word == outputs[i].alias || word == String(i + 1))
      return i;
  return -1;
}

void setTestMode(bool enabled)
{
  testMode = enabled;
  Serial.println("Test mode: " + String(testMode ? "on" : "off"));
  clearOutputs();

  if (testMode)
  {
    // everything dark so the operator starts from a known state
    applyOutputs();
  }
  else
  {
    previousLightState = OFF;
    currentLightState = OFF;
#ifdef PED_SIGNAL_ENABLED
    setPedState(PED_DONT_WALK);
#endif
  }
}

bool setTestOutput(const String &output, bool state)
{
  if (output == "all_off")
    clearOutputs();
  else
  {
    int i = findOutput(output);
    if (i < 0)
      return false;
    held[i] = state;
    flashing[i] = false;
  }
  applyOutputs();
  return true;
}

bool testOutputHeld(const String &output)
{
  int i = findOutput(output);
  return i >= 0 && held[i];
}

void updateTestFlash(unsigned long now)
{
  if (now - lastFlash < pedFdwFlashInterval)
    return;
  lastFlash = now;
  flashOn = !flashOn;

  for (int i = 0; i < outputCount; i++)
    if (held[i] && flashing[i])
    {
      applyOutputs();
      return;
    }
}

// The two hots of a ped head are never on together, so lighting one drops the other
// and the status line shows what the relays are really doing.
static void dropPartner(int i)
{
  if (i < 3)
    return;
  int partner = i % 2 ? i + 1 : i - 1;
  held[partner] = flashing[partner] = false;
}

static void setFlashing(int i, bool on)
{
  held[i] = flashing[i] = on;
  if (on)
    dropPartner(i);
}

static void printStatus()
{
  String line = testMode ? "TEST" : "RUN ";
  for (int i = 0; i < outputCount; i++)
  {
    const char *state = !held[i] ? "off" : flashing[i] ? "FLASH" : "ON";
    line += "  " + String(i + 1) + " " + outputs[i].alias + "(" + String(outputs[i].pin) + "):" + state;
  }
  Serial.println(line);
}

static void printHelp()
{
  Serial.println("Commands:");
  Serial.println("  <output>          toggle one relay");
  Serial.println("  <output> on|off   set one relay");
  Serial.println("  flash <output>    toggle flashing on one relay");
  Serial.println("  flash             flashing hand on both ped heads (starts the countdown)");
  Serial.println("  off               everything off");
  Serial.println("  run               leave test mode, back to the normal cycle");
#ifdef PED_SIGNAL_ENABLED
  Serial.println("  set walk|fdw|dw|delay <seconds>   pedestrian times, saved");
  Serial.println("                    fdw is the countdown, delay the wait after the light changes");
#endif
  Serial.println("  status, id, help");
  String names = "Outputs:";
  for (int i = 0; i < outputCount; i++)
    names += "  " + String(i + 1) + "=" + outputs[i].alias;
  Serial.println(names);
  Serial.println("Any output command switches test mode on.");
}

#ifdef PED_SIGNAL_ENABLED
// "set fdw 5": change one pedestrian time and save it, as the settings page would.
// Bounds match the settings form.
struct PedTime
{
  const char *name;
  const char *pref;
  unsigned long *value;
  long minSeconds, maxSeconds;
};

static const PedTime pedTimes[] = {
    {"walk", "ped_walk", &ped_walk_duration, 0, 120},
    {"fdw", "ped_fdw", &ped_fdw_duration, 3, 99},
    {"dw", "ped_dw", &ped_dw_duration, 1, 60},
    {"delay", "ped_delay", &ped_start_delay, 0, (long)PED_MAX_START_DELAY_S},
};

static void setPedTime(String arg)
{
  int space = arg.indexOf(' ');
  String which = space < 0 ? arg : arg.substring(0, space);
  String number = space < 0 ? "" : arg.substring(space + 1);
  long seconds = number.toInt();

  for (const PedTime &t : pedTimes)
  {
    if (which != t.name)
      continue;
    // toInt() gives 0 for text, so a 0 only counts when it was typed as one.
    if (number.isEmpty() || (seconds == 0 && number != "0") || seconds < t.minSeconds || seconds > t.maxSeconds)
    {
      Serial.println(String(t.name) + " takes " + String(t.minSeconds) + " to " + String(t.maxSeconds) + " seconds");
      return;
    }
    *t.value = seconds * 1000UL;
    preferences.putULong(t.pref, *t.value);
    if (which == "walk")
      ped_walk_effective = ped_walk_duration;
    Serial.println("Ped times: walk=" + String(ped_walk_duration / 1000) + "s fdw=" +
                   String(ped_fdw_duration / 1000) + "s dw=" + String(ped_dw_duration / 1000) +
                   "s delay=" + String(ped_start_delay / 1000) + "s");
    return;
  }
  Serial.println("Usage: set walk|fdw|dw|delay <seconds>");
}
#endif

static void runCommand(String line)
{
  line.trim();
  line.toLowerCase();
  if (line.isEmpty())
    return;

  int space = line.indexOf(' ');
  String word = space < 0 ? line : line.substring(0, space);
  String arg = space < 0 ? "" : line.substring(space + 1);
  arg.trim();

  if (word == "id")
  {
    Serial.println("IDENTITY " + identityJson());
    return;
  }
  if (word == "help" || word == "?")
  {
    printHelp();
    return;
  }
  if (word == "status")
  {
    printStatus();
    return;
  }
  if (word == "run")
  {
    setTestMode(false);
    return;
  }
#ifdef PED_SIGNAL_ENABLED
  if (word == "set")
  {
    setPedTime(arg);
    return;
  }
#endif

  bool isFlash = word == "flash";
  int target = findOutput(isFlash ? arg : word);
  bool known = word == "off" || target >= 0 || (isFlash && arg.isEmpty());
#ifndef PED_SIGNAL_ENABLED
  if (isFlash && arg.isEmpty())
    known = false;
#endif
  if (!known)
  {
    Serial.println("Unknown command: " + line + " (try help)");
    return;
  }

  if (!testMode)
    setTestMode(true);

  if (word == "off")
    clearOutputs();
  else if (isFlash && target < 0)
  {
#ifdef PED_SIGNAL_ENABLED
    // The clearance interval as the heads see it: both hands flashing, both walks dark.
    int hand = findOutput("dont_walk"), cdHand = findOutput("cd_dont_walk");
    bool on = !(flashing[hand] && flashing[cdHand]);
    setFlashing(hand, on);
    setFlashing(cdHand, on);
#endif
  }
  else if (isFlash)
    setFlashing(target, !flashing[target]);
  else
  {
    held[target] = arg == "on" ? true : arg == "off" ? false : !held[target];
    flashing[target] = false;
    if (held[target])
      dropPartner(target);
  }

  // Restart the flash on its lit half so a new flasher shows up at once.
  flashOn = true;
  lastFlash = millis();
  applyOutputs();
  printStatus();
}

void pollSerialConsole()
{
  static String line;
  while (Serial.available())
  {
    char c = Serial.read();
    if (c == '\n' || c == '\r')
    {
      runCommand(line);
      line = "";
    }
    else if (line.length() < 32)
      line += c;
  }
}
