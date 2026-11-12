"""Writes fakes.inc: ISteamInventory/Apps/Friends/User/Utils/UserStats subclasses for the fake libsteam_api,
generated from the Steamworks headers so every pure virtual is overridden.
Usage: gen_fakes.py <sdk>/public/steam <out file>"""
import re, sys
H = sys.argv[1].rstrip("/") + "/"
custom = {
 'ISteamInventory': {
  'GetResultStatus': 'return k_EResultOK;',
  'GetResultItems': 'return fake_GetResultItems(resultHandle, pOutItemsArray, punOutItemsArraySize);',
  'DestroyResult': 'fake_destroy(resultHandle);',
  'GetAllItems': 'return fake_GetAllItems(pResultHandle);',
  'ConsumeItem': 'return fake_ConsumeItem(pResultHandle, itemConsume, unQuantity);',
  'LoadItemDefinitions': 'return true;',
  'GetItemDefinitionProperty': 'return fake_DefProperty(iDefinition, pchPropertyName, pchValueBuffer, punValueBufferSizeOut);',
  'StartPurchase': 'return fake_StartPurchase(pArrayItemDefs, punArrayQuantity, unArrayLength);',
  'RequestPrices': 'return fake_RequestPrices();',
  'GetItemPrice': 'return fake_GetItemPrice(iDefinition, pCurrentPrice, pBasePrice);',
 },
 'ISteamApps': {
  'BIsDlcInstalled': 'return fake_dlcOwned(appID);',
  'GetDLCCount': 'return 1;',
  'BGetDLCDataByIndex': 'if (pAppID) *pAppID = 777; if (pbAvailable) *pbAvailable = true; snprintf(pchName, cchNameBufferSize, "Expansion"); return iDLC == 0;',
 },
 'ISteamFriends': {
  'ActivateGameOverlayToStore': 'fake_overlayStore(nAppID);',
  'GetPersonaName': 'return "Tester";',
  'GetFriendCount': 'return 2;',
  'GetFriendByIndex': 'return CSteamID(uint64(76561197960287931ULL + iFriend));',
  'GetFriendPersonaState': 'return steamIDFriend.ConvertToUint64() == 76561197960287931ULL ? k_EPersonaStateOnline : k_EPersonaStateOffline;',
  'GetFriendPersonaName': 'return steamIDFriend.ConvertToUint64() == 76561197960287931ULL ? "Ally" : "Rival";',
  'GetFriendGamePlayed': 'if (steamIDFriend.ConvertToUint64() != 76561197960287931ULL) return false; *pFriendGameInfo = FriendGameInfo_t{}; pFriendGameInfo->m_gameID = CGameID(480u); return true;',
  'GetFriendRichPresence': 'return fake_presence(steamIDFriend, pchKey);',
  'SetRichPresence': 'return fake_setPresence(pchKey, pchValue);',
  'ClearRichPresence': 'fake_clearPresence();',
  'ActivateGameOverlay': 'fake_overlayDialog(pchDialog);',
  'ActivateGameOverlayToWebPage': 'fake_overlayDialog(pchURL);',
  'ActivateGameOverlayInviteDialogConnectString': 'fake_overlayDialog(pchConnectString);',
  'InviteUserToGame': 'fake_overlayDialog(pchConnectString); return true;',
 },
 'ISteamUser': { 'GetSteamID': 'return CSteamID(uint64(76561197960287930ULL));' },
 'ISteamUtils': { 'GetAppID': 'return 480;', 'GetIPCountry': 'return "DE";' },
 'ISteamUserStats': {
  'GetStat': 'return fake_GetStat(pchName, pData);',
  'SetStat': 'return fake_SetStat(pchName, ' + "{}" + ');',
  'GetAchievement': 'return fake_GetAchievement(pchName, pbAchieved);',
  'SetAchievement': 'return fake_SetAchievement(pchName);',
  'StoreStats': 'return fake_StoreStats();',
  'GetAchievementAndUnlockTime': 'return fake_AchTime(pchName, pbAchieved, punUnlockTime);',
  'GetAchievementDisplayAttribute': 'return std::string(pchKey) == "name" ? pchName : "desc";',
  'IndicateAchievementProgress': 'return fake_Progress(pchName, nCurProgress, nMaxProgress);',
  'GetNumAchievements': 'return 2;',
  'GetAchievementName': 'return iAchievement == 0 ? "ACH_WIN_ONE_GAME" : iAchievement == 1 ? "ACH_TRAVEL" : nullptr;',
 },
}
files = {'ISteamInventory':'isteaminventory.h','ISteamApps':'isteamapps.h','ISteamFriends':'isteamfriends.h','ISteamUser':'isteamuser.h','ISteamUtils':'isteamutils.h','ISteamUserStats':'isteamuserstats.h'}
out = []
for cls, f in files.items():
    t = open(H+f, encoding='latin-1').read()
    m = re.search(r'class\s+'+cls+r'\s*\{(.*?)\n\};', t, re.S)
    body = m.group(1)
    body = re.sub(r'//[^\n]*', '', body)
    decls = re.findall(r'virtual\s+(.*?)\s*=\s*0\s*;', body, re.S)
    out.append(f'class Fake{cls} : public {cls} {{ public:')
    for d in decls:
        d = ' '.join(d.split())
        name = re.search(r'(\w+)\s*\(', d).group(1)
        ret = d[:d.index(name)].strip()
        # drop default args
        d2 = re.sub(r'\s*=\s*[^,)]+', '', d)
        if name in custom[cls]: b = custom[cls][name]
        elif ret == 'void': b = ''
        else: b = 'return {};'
        if name == 'SetStat': b = 'return fake_SetStat(pchName, %s);' % ('nData' if 'nData' in d2 else 'fData')
        out.append(f'  {d2} override {{ {b} }}')
    out.append('};')
open(sys.argv[2],'w').write('\n'.join(out))
