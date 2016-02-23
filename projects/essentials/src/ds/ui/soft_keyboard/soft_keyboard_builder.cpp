#include "soft_keyboard_builder.h"

#include <ds/ui/sprite/sprite_engine.h>
#include <ds/ui/sprite/image.h>
#include <ds/app/environment.h>
#include <ds/debug/logger.h>

#include "ds/ui/soft_keyboard/soft_keyboard.h"

namespace ds {
namespace ui {
namespace SoftKeyboardBuilder {

SoftKeyboard* buildLowercaseKeyboard(ds::ui::SpriteEngine& engine, SoftKeyboardSettings& settings, ds::ui::Sprite* parent) {
	SoftKeyboard* newKeyboard = new SoftKeyboard(engine, settings);

	float xp = settings.mKeyInitialPosition.x;
	float yp = settings.mKeyInitialPosition.y;

	makeAButton(engine, newKeyboard, xp, yp, L"1", L"!", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L"2", L"@", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L"3", L"#", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L"4", L"$", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L"5", L"%", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L"6", L"^", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L"7", L"&", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L"8", L"*", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L"9", L"(", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L"0", L")", SoftKeyboardDefs::kNumber);

	xp = settings.mKeyInitialPosition.x;
	yp += newKeyboard->getButtonVector().back()->getHeight();

	makeAButton(engine, newKeyboard, xp, yp, L"q", L"Q", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"w", L"W", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"e", L"E", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"r", L"R", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"t", L"T", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"y", L"Y", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"u", L"U", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"i", L"I", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"o", L"O", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"p", L"P", SoftKeyboardDefs::kLetter);

	xp = settings.mKeyInitialPosition.x + newKeyboard->getButtonVector().back()->getWidth() / 2.0f;
	yp += newKeyboard->getButtonVector().back()->getHeight();

	makeAButton(engine, newKeyboard, xp, yp, L"a", L"A", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"s", L"S", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"d", L"D", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"f", L"F", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"g", L"G", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"h", L"H", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"j", L"J", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"k", L"K", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"l", L"L", SoftKeyboardDefs::kLetter);

	xp = settings.mKeyInitialPosition.x + newKeyboard->getButtonVector().back()->getWidth()* 3.0f / 2.0f;
	yp += newKeyboard->getButtonVector().back()->getHeight();

	makeAButton(engine, newKeyboard, xp, yp, L"z", L"Z", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"x", L"X", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"c", L"C", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"v", L"V", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"b", L"B", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"n", L"N", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"m", L"M", SoftKeyboardDefs::kLetter);

	xp = settings.mKeyInitialPosition.x;
	yp += newKeyboard->getButtonVector().back()->getHeight();

	makeAButton(engine, newKeyboard, xp, yp, L"-", L"_", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L",", L"<", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L".", L">", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L"/", L"?", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L"space", L"SPACE", SoftKeyboardDefs::kSpace);
	makeAButton(engine, newKeyboard, xp, yp, L"", L"", SoftKeyboardDefs::kDelete);

	newKeyboard->setSize(xp, yp);

	if(parent){
		parent->addChildPtr(newKeyboard);
	}

	return newKeyboard;
}

SoftKeyboard* buildStandardKeyboard(ds::ui::SpriteEngine& engine, SoftKeyboardSettings& settings, ds::ui::Sprite* parent) {
	SoftKeyboard* newKeyboard = new SoftKeyboard(engine, settings);

	float xp = settings.mKeyInitialPosition.x;
	float yp = settings.mKeyInitialPosition.y;

	makeAButton(engine, newKeyboard, xp, yp, L"1", L"!", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L"2", L"@", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L"3", L"#", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L"4", L"$", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L"5", L"%", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L"6", L"^", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L"7", L"&", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L"8", L"*", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L"9", L"(", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L"0", L")", SoftKeyboardDefs::kNumber);

	xp = settings.mKeyInitialPosition.x;
	yp += newKeyboard->getButtonVector().back()->getHeight();

	makeAButton(engine, newKeyboard, xp, yp, L"q", L"Q", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"w", L"W", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"e", L"E", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"r", L"R", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"t", L"T", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"y", L"Y", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"u", L"U", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"i", L"I", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"o", L"O", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"p", L"P", SoftKeyboardDefs::kLetter);

	xp = settings.mKeyInitialPosition.x + newKeyboard->getButtonVector().back()->getWidth() / 2.0f;
	yp += newKeyboard->getButtonVector().back()->getHeight();

	makeAButton(engine, newKeyboard, xp, yp, L"a", L"A", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"s", L"S", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"d", L"D", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"f", L"F", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"g", L"G", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"h", L"H", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"j", L"J", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"k", L"K", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"l", L"L", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"'", L"\"", SoftKeyboardDefs::kLetter);

	xp = settings.mKeyInitialPosition.x - newKeyboard->getButtonVector().back()->getWidth() / 2.0f;
	yp += newKeyboard->getButtonVector().back()->getHeight();

	makeAButton(engine, newKeyboard, xp, yp, L"shift", L"SHIFT", SoftKeyboardDefs::kShift);
	makeAButton(engine, newKeyboard, xp, yp, L"z", L"Z", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"x", L"X", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"c", L"C", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"v", L"V", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"b", L"B", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"n", L"N", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"m", L"M", SoftKeyboardDefs::kLetter);
	makeAButton(engine, newKeyboard, xp, yp, L"enter", L"ENTER", SoftKeyboardDefs::kEnter);

	xp = settings.mKeyInitialPosition.x;
	yp += newKeyboard->getButtonVector().back()->getHeight();

	makeAButton(engine, newKeyboard, xp, yp, L"-", L"_", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L",", L"<", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L".", L">", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L"/", L"?", SoftKeyboardDefs::kNumber);
	makeAButton(engine, newKeyboard, xp, yp, L"space", L"SPACE", SoftKeyboardDefs::kSpace);
	makeAButton(engine, newKeyboard, xp, yp, L"", L"", SoftKeyboardDefs::kDelete);

	newKeyboard->setSize(xp, yp);

	newKeyboard->setScale(settings.mKeyScale);
	
	if(parent){
		parent->addChildPtr(newKeyboard);
	}

	return newKeyboard;
}

void makeAButton(ds::ui::SpriteEngine& engine, SoftKeyboard* newKeyboard, float& xp,float& yp, const std::wstring& character, const std::wstring& characterHigh, const SoftKeyboardDefs::KeyType keyType){
	SoftKeyboardButton* kb = new SoftKeyboardButton(engine, character, characterHigh, keyType, newKeyboard->getSoftKeyboardSettings());
	kb->setClickFn([newKeyboard, kb](){newKeyboard->handleKeyPress(kb); });
	kb->setPosition(xp + kb->getScaleWidth() / 2.0f, yp + kb->getScaleHeight() / 2.0f);
	xp += kb->getWidth();
	newKeyboard->addChild(*kb);
	newKeyboard->getButtonVector().push_back(kb);

}

}
}
}