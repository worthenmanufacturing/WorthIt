/*
    This file is part of Repetier-Firmware.

    Repetier-Firmware is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Repetier-Firmware is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Repetier-Firmware.  If not, see <http://www.gnu.org/licenses/>.

*/

#ifndef IO_TARGET
#error You need to set IO_TARGET before calling this file!
#endif

#undef TONE_THEME
#undef TONE_THEME_COND
#undef BEEPER_SOURCE_IO
#undef BEEPER_SOURCE_PWM
#undef TONES // helper macro for init lists

#if IO_TARGET == IO_TARGET_CLASS_DEFINITION

constexpr fast8_t beepBufSize = 50;

struct TonePacket {
    //If the frequency is 0, it'll behave as putting a G4 P(duration) command in between M300's.
    uint16_t frequency;
    uint16_t duration;
};

class ToneTheme {
public:
    template <fast8_t size>
    ToneTheme(const TonePacket (&theme)[size])
        : savedTheme(theme)
        , themeSize(size) {
    }
    const TonePacket getTone(fast8_t index) {
        TonePacket beep;
        memcpy_P(&beep, &savedTheme[index], sizeof(TonePacket));
        return beep;
    }
    const inline fast8_t getSize() { return themeSize; }

private:
    const TonePacket* savedTheme;
    const fast8_t themeSize;
};

struct ToneCondition {
    bool started;
    const bool loop;
    bool heard;
    fast8_t plays;
    const fast8_t playCount;
    ToneTheme* theme;
};

class BeeperSourceBase {
public:
    BeeperSourceBase()
        : playing(false)
        , halted(false)
        , muted(false)
        , blocking(false)
        , toneHead(-1)
        , toneTail(-1)
        , prevToneTime(0)
        , playingFreq(0)
        , lastConditionStep(0)
        , curConditionStep(0) { }
    virtual inline fast8_t getHeadDist() {
        return !isPlaying() ? 0 : (toneHead >= toneTail ? (toneHead - toneTail) : (beepBufSize - toneTail + toneHead));
    }
    virtual inline ufast8_t getOutputType() { return 0; };
    virtual inline uint16_t getCurFreq() { return playingFreq; }
    virtual inline volatile bool isPlaying() { return playing; }
    virtual inline volatile bool isHalted() { return halted; }
    virtual inline bool isMuted() { return muted; }
    virtual inline bool isBlocking() { return blocking; }
    virtual inline bool mute(bool set) {
        if (set && isPlaying()) {
            finishPlaying(); // Kill any timers/pwm only if we're playing.
        }
        return (muted = set);
    }
    virtual bool pushTone(const TonePacket packet);
    virtual bool playTheme(ToneTheme& theme, bool block);
    virtual fast8_t process();
    virtual ufast8_t getFreqDiv() = 0;
    virtual void setFreqDiv(ufast8_t div) = 0;
    virtual void setCondition(bool set, ToneCondition& cond);
    virtual void runConditions();

protected:
    virtual void refreshBeepFreq() = 0;
    virtual void finishPlaying();
    volatile bool playing;
    volatile bool halted; // Special state between beeps/duration only beeps.
    bool muted;           // eeprom etc
    bool blocking;
    fast8_t toneHead;
    fast8_t toneTail;
    millis_t prevToneTime;
    uint16_t playingFreq;
    TonePacket beepBuf[beepBufSize] {};
    fast8_t lastConditionStep;
    fast8_t curConditionStep;
    ToneCondition* curValidCondition;
    ToneCondition* lastValidCondition;
};

template <class IOPin>
class BeeperSourceIO : public BeeperSourceBase {
public:
    BeeperSourceIO()
        : BeeperSourceBase()
        , freqCnt(0)
        , freqDiv(0)
        , pinState(false) {
        IOPin::off();
    };
    virtual inline ufast8_t getOutputType() final {
        return 1;
    }
    virtual inline ufast8_t getFreqDiv() final {
        return freqDiv;
    }
    virtual inline void setFreqDiv(ufast8_t div) final {
        freqDiv = div;
    }
    inline void toggle() {
        freqCnt = 0;
        IOPin::set(pinState = !pinState);
    }
    volatile uint8_t freqCnt;

private:
    virtual void refreshBeepFreq() final {
        InterruptProtectedBlock noInts;
        if (playingFreq > 0) {
            halted = false;
            freqDiv = 0;
            HAL::tone(playingFreq);
        } else { // Turn off and just wait if we have no frequency.
            HAL::noTone();
            IOPin::set(pinState = (freqCnt = 0));
            halted = true;
        }
    }
    virtual void finishPlaying() final {
        InterruptProtectedBlock noInts;
        BeeperSourceBase::finishPlaying();
        HAL::noTone();
        IOPin::off();
        freqDiv = freqCnt = 0;
    }
    volatile ufast8_t freqDiv;
    volatile bool pinState;
};

class BeeperSourcePWM : public BeeperSourceBase {
public:
    BeeperSourcePWM(PWMHandler* pwm)
        : BeeperSourceBase()
        , pwmPin(pwm) {
        pwmPin->set(0);
    };
    virtual inline ufast8_t getOutputType() final { return 2; }
    virtual inline ufast8_t getFreqDiv() final { return 0; }
    virtual inline void setFreqDiv(ufast8_t div) final { }

private:
    virtual void refreshBeepFreq() final;
    virtual inline void finishPlaying() final {
        pwmPin->set(0);
        BeeperSourceBase::finishPlaying();
    }
    PWMHandler* pwmPin;
};

#define PLAY_THEME(source, theme, blocking) \
    source.playTheme(theme, blocking);

#define BEEPER_SOURCE_IO(name, IOPin) \
    extern BeeperSourceIO<IOPin> name;

#define BEEPER_SOURCE_PWM(name, PWMPin) \
    extern BeeperSourcePWM name;

#define TONE_THEME(name, theme) \
    extern ToneTheme name;

#define TONE_THEME_COND(name, source, cond, theme, playTimes) \
    extern ToneCondition name##_cond;

#elif IO_TARGET == IO_TARGET_DEFINE_VARIABLES

#define TONES(...) \
    { __VA_ARGS__ }

#define BEEPER_SOURCE_IO(name, IOPin) \
    BeeperSourceIO<IOPin> name; \
    static_assert(NUM_BEEPERS, "\"" #name "\" created in config_io but NUM_BEEPERS is zero!");

#define BEEPER_SOURCE_PWM(name, PWMPin) \
    BeeperSourcePWM name(&PWMPin); \
    static_assert(NUM_BEEPERS, "\"" #name "\" created in config_io but NUM_BEEPERS is zero!");

#define TONE_THEME(name, theme) \
    constexpr TonePacket name##_theme[] PROGMEM = theme; \
    ToneTheme name(name##_theme); \
    static_assert(!((sizeof(name##_theme) / sizeof(TonePacket)) > beepBufSize), "Length of \"" #name "\" is larger than beeper buffer size!");

#define TONE_THEME_COND(name, source, cond, theme, playTimes) \
    ToneCondition name##_cond = { false, (playTimes == 0), false, 0, playTimes, &theme };

#elif IO_TARGET == IO_TARGET_SERVO_INTERRUPT // IO_TARGET_PERIODICAL_ACTIONS

#define BEEPER_SOURCE_IO(name, IOPin) \
    name.process();

#define BEEPER_SOURCE_PWM(name, PWMPin) \
    name.process();

#elif IO_TARGET == IO_TARGET_100MS

#define BEEPER_SOURCE_IO(name, IOPin) \
    name.runConditions();

#define BEEPER_SOURCE_PWM(name, PWMPin) \
    name.runConditions();

#define TONE_THEME_COND(name, source, cond, theme, playTimes) \
    if (!source.isMuted()) { \
        source.setCondition(cond, name##_cond); \
    }

#elif IO_TARGET == IO_TARGET_BEEPER_LOOP

#define BEEPER_SOURCE_IO(name, IOPin) \
    if (name.isPlaying() && !name.isHalted() && name.freqCnt++ >= name.getFreqDiv()) { \
        name.toggle(); \
    }

#endif

#ifndef PLAY_THEME
#define PLAY_THEME(source, theme, blocking)
#endif

#ifndef TONE_THEME_COND
#define TONE_THEME_COND(name, source, cond, theme, playTimes)
#endif

#ifndef TONE_THEME
#define TONE_THEME(name, theme)
#endif

#ifndef BEEPER_SOURCE_IO
#define BEEPER_SOURCE_IO(name, IOPin)
#endif

#ifndef BEEPER_SOURCE_PWM
#define BEEPER_SOURCE_PWM(name, PWMPin)
#endif

#if !defined(CUSTOM_DEFAULT_THEMES) || CUSTOM_DEFAULT_THEMES == 0
TONE_THEME(ThemeButtonNextPrev, TONES({ 3000, 1 }))
TONE_THEME(ThemeButtonOk, TONES({ 3000, 10 }))
TONE_THEME(ThemeButtonReset, TONES({ 6500, 50 }, { 7500, 80 }))
TONE_THEME(ThemeNotifyWarning, TONES({ 1000, 300 }))
TONE_THEME(ThemeNotifyError, TONES({ 3050, 150 }, { 7200, 100 }, { 3050, 150 }, { 2000, 100 }, { 3050, 150 }))
TONE_THEME(ThemeNotifyConfirm, TONES({ 3050, 50 }, { 4000, 30 }))
#endif