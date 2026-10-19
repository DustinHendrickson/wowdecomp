// macOS stand-ins for CGLCD (UILCD/LCD.cpp), the Logitech G-series keyboard LCD support, which is built on the
// Windows-only Logitech LCD SDK. With no LCD to drive, every notification is dropped, as on a Windows machine without
// such a keyboard.

#include "WoW/Source/Ui/UILCD/LCD.h"

void CGLCD::Initialize() {
}

void CGLCD::Shutdown() {
}

void CGLCD::Login() {
}

void CGLCD::Logout() {
}

void CGLCD::EnterWorld() {
}

void CGLCD::LeaveWorld() {
}

void CGLCD::ClearWaitQueueScreen() {
}

void CGLCD::IdleDisconnectWarning() {
}

void CGLCD::ShowBattlefieldScreen() {
}

void CGLCD::HideBattlefieldAlert() {
}

void CGLCD::OnBattlefieldReady(const BattlefieldStatus* status) {
}

void CGLCD::OnBattlefieldActive() {
}

void CGLCD::OnWhisper(const char* name) {
}

void CGLCD::AuctionOutbid(const char* name) {
}

void CGLCD::AuctionExpired(const char* name) {
}

void CGLCD::AuctionSold(const char* name) {
}

void CGLCD::AuctionWon(const char* name) {
}
