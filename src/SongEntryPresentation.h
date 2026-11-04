#pragma once
namespace bms::ui {
enum class SongEntryKind { Song, Folder, ParentFolder, AllSongs, LibraryRoot };
inline SongEntryKind navigationEntryKind(int id){return id==-1?SongEntryKind::ParentFolder:id==-2?SongEntryKind::AllSongs:SongEntryKind::Folder;}
inline const wchar_t* entryTypeLabel(SongEntryKind kind){switch(kind){case SongEntryKind::Folder:return L"DIR";case SongEntryKind::LibraryRoot:return L"LIB";case SongEntryKind::ParentFolder:return L"..";default:return L"";}}
}
