#include <ui/ui_MenuLayout.hpp>
#include <os/os_Titles.hpp>
#include <util/util_Convert.hpp>
#include <ui/ui_QMenuApplication.hpp>
#include <os/os_HomeMenu.hpp>
#include <fs/fs_Stdio.hpp>

extern ui::QMenuApplication::Ref qapp;
extern cfg::TitleList list;

namespace ui
{
    MenuLayout::MenuLayout(void *raw)
    {
        this->susptr = raw;
        this->mode = 0;
        this->rawalpha = 255;
        this->root_idx = 0;
        this->warnshown = false;

        this->bgSuspendedRaw = RawData::New(0, 0, raw, 1280, 720, 4);
        this->Add(this->bgSuspendedRaw);

        this->itemsMenu = SideMenu::New(pu::ui::Color(0, 255, 120, 255), "romfs:/default/ui/Cursor.png");
        this->MoveFolder("", false);
        
        this->itemsMenu->SetOnItemSelected(std::bind(&MenuLayout::menu_Click, this, std::placeholders::_1, std::placeholders::_2));
        this->Add(this->itemsMenu);
        this->tp = std::chrono::steady_clock::now();

        this->SetOnInput(std::bind(&MenuLayout::OnInput, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
    }

    void MenuLayout::menu_Click(u64 down, u32 index)
    {
        auto &folder = cfg::FindFolderByName(list, this->curfolder);
        if(index == 0)
        {
            if(down & KEY_A)
            {
                qapp->CreateShowDialog("A", "All titles...", {"Ok"}, true);
            }
        }
        else
        {
            u32 realidx = index - 1;
            if(realidx < folder.titles.size())
            {
                auto title = folder.titles[realidx];
                if(down & KEY_A)
                {
                    if(!qapp->IsTitleSuspended())
                    {
                        if((cfg::TitleType)title.title_type == cfg::TitleType::Homebrew)
                        {
                            // TODO: Homebrew launching!
                        }
                        else
                        {
                            am::QMenuCommandWriter writer(am::QDaemonMessage::LaunchApplication);
                            writer.Write<u64>(title.app_id);
                            writer.Write<bool>(false);
                            writer.FinishWrite();

                            am::QMenuCommandResultReader reader;
                            if(reader && R_SUCCEEDED(reader.GetReadResult()))
                            {
                                qapp->CloseWithFadeOut();
                                return;
                            }
                            else
                            {
                                auto rc = reader.GetReadResult();
                                qapp->CreateShowDialog("Title launch", "An error ocurred attempting to launch the title:\n" + util::FormatResultDisplay(rc) + " (" + util::FormatResultHex(rc) + ")", { "Ok" }, true);
                            }
                            reader.FinishRead();
                        }
                    }
                    else if(qapp->GetSuspendedApplicationId() == title.app_id)
                    {
                        // Pressed A on the suspended title - return to it
                        if(this->mode == 1) this->mode = 2;
                    }
                }
                else if(down & KEY_X)
                {
                    if(this->HandleFolderChange(title))
                    {
                        this->MoveFolder(this->curfolder, true);
                    }
                }
            }
            else
            {
                auto foldr = list.folders[realidx - folder.titles.size()];
                if(down & KEY_A)
                {
                    this->MoveFolder(foldr.name, true);
                }
            }
        }
    }

    void MenuLayout::MoveFolder(std::string name, bool fade)
    {
        if(fade) qapp->FadeOut();

        if(this->curfolder.empty())
        {
            // Moving from root to a folder, let's save the indexes we were on
            this->root_idx = itemsMenu->GetSelectedItem();
            this->root_baseidx = itemsMenu->GetBaseItemIndex();
        }

        auto &folder = cfg::FindFolderByName(list, name);
        this->itemsMenu->ClearItems();

        // Add first item for all titles menu
        this->itemsMenu->AddItem("romfs:/default/ui/AllTitles.png");
        u32 tmpidx = 0;
        for(auto itm: folder.titles)
        {
            std::string iconpth;
            if((cfg::TitleType)itm.title_type == cfg::TitleType::Installed) iconpth = cfg::GetTitleCacheIconPath(itm.app_id);
            else if((cfg::TitleType)itm.title_type == cfg::TitleType::Homebrew) iconpth = cfg::GetNROCacheIconPath(itm.nro_path);
            this->itemsMenu->AddItem(iconpth);
            if(qapp->GetSuspendedApplicationId() == itm.app_id) this->itemsMenu->SetSuspendedItem(tmpidx + 1); // 1st item is always "all titles"!
            tmpidx++;
        }
        if(name.empty())
        {
            std::vector<cfg::TitleFolder> folders;
            for(auto folder: list.folders)
            {
                if(!folder.titles.empty())
                {
                    folders.push_back(folder);
                    this->itemsMenu->AddItem("romfs:/default/ui/Folder.png");
                }
            }
            list.folders = folders;
            this->itemsMenu->SetSelectedItem(this->root_idx);
            this->itemsMenu->SetBaseItemIndex(this->root_baseidx);
        }
        this->itemsMenu->UpdateBorderIcons();

        this->curfolder = name;

        if(fade) qapp->FadeIn();
    }

    void MenuLayout::OnInput(u64 down, u64 up, u64 held, pu::ui::Touch pos)
    {
        auto ctp = std::chrono::steady_clock::now();
        if(std::chrono::duration_cast<std::chrono::milliseconds>(ctp - this->tp).count() >= 500)
        {
            if(qapp->LaunchFailed() && !this->warnshown)
            {
                qapp->CreateShowDialog("Title launch", "The title failed to start.\nAre you sure it can be launched? (it isn't deleted, gamecard is inserted...)", {"Ok"}, true);
                this->warnshown = true;
            }
        }

        auto [rc, msg] = am::QMenu_GetLatestQMenuMessage();
        switch(msg)
        {
            case am::QMenuMessage::HomeRequest:
            {
                if(qapp->IsTitleSuspended())
                {
                    if(this->mode == 1) this->mode = 2;
                }
                else while(this->itemsMenu->GetSelectedItem() > 0) this->itemsMenu->HandleMoveLeft();
                break;
            }
            default:
                break;
        }

        if(this->susptr != NULL)
        {
            if(this->mode == 0)
            {
                if(this->rawalpha == 80) this->mode = 1;
                else
                {
                    this->bgSuspendedRaw->SetAlphaFactor(this->rawalpha);
                    this->rawalpha -= 10;
                    if(this->rawalpha < 80) this->rawalpha = 80;
                    else
                    {
                        s32 cw = this->bgSuspendedRaw->GetWidth();
                        s32 ch = this->bgSuspendedRaw->GetHeight();
                        // 16:9 ratio
                        cw -= 16;
                        ch -= 9;
                        s32 x = (1280 - cw) / 2;
                        s32 y = (720 - ch) / 2;
                        this->bgSuspendedRaw->SetX(x);
                        this->bgSuspendedRaw->SetY(y);
                        this->bgSuspendedRaw->SetWidth(cw);
                        this->bgSuspendedRaw->SetHeight(ch);
                    }
                }
            }
            else if(this->mode == 2)
            {
                if(this->rawalpha == 255)
                {
                    this->bgSuspendedRaw->SetAlphaFactor(this->rawalpha);
                    am::QMenuCommandWriter writer(am::QDaemonMessage::ResumeApplication);
                    writer.FinishWrite();

                    am::QMenuCommandResultReader reader;
                    reader.FinishRead();
                }
                else
                {
                    this->bgSuspendedRaw->SetAlphaFactor(this->rawalpha);
                    this->rawalpha += 10;
                    if(this->rawalpha > 255) this->rawalpha = 255;
                    else
                    {
                        s32 cw = this->bgSuspendedRaw->GetWidth();
                        s32 ch = this->bgSuspendedRaw->GetHeight();
                        // 16:9 ratio
                        cw += 16;
                        ch += 9;
                        s32 x = (1280 - cw) / 2;
                        s32 y = (720 - ch) / 2;
                        this->bgSuspendedRaw->SetX(x);
                        this->bgSuspendedRaw->SetY(y);
                        this->bgSuspendedRaw->SetWidth(cw);
                        this->bgSuspendedRaw->SetHeight(ch);
                    }
                }
            }
        }

        if(down & KEY_B)
        {
            if(!this->curfolder.empty()) this->MoveFolder("", true);
        }
        else if(down & KEY_L)
        {
            qapp->CreateShowDialog("A", "Id -> " + util::FormatApplicationId(qapp->GetSuspendedApplicationId()), {"Ok"}, true);
        }
    }

    bool MenuLayout::HandleFolderChange(cfg::TitleRecord &rec)
    {
        bool changedone = false;

        if(this->curfolder.empty())
        {
            SwkbdConfig swkbd;
            swkbdCreate(&swkbd, 0);
            swkbdConfigSetHeaderText(&swkbd, "Select directory name");
            char dir[500] = {0};
            auto rc = swkbdShow(&swkbd, dir, 500);
            if(R_SUCCEEDED(rc))
            {
                cfg::MoveTitleToDirectory(list, rec.app_id, std::string(dir));
                changedone = true;
            }
        }
        else
        {
            auto sopt = qapp->CreateShowDialog("Title move", "Would you like to move this entry outside the folder?", { "Yes", "Cancel" }, true);
            if(sopt == 0)
            {
                cfg::MoveTitleToDirectory(list, rec.app_id, "");
                changedone = true;
            }
        }

        return changedone;
    }
}