// Copyright (c) 2014-2018 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include "sax_parser.h"
#include <glibmm/convert.h> //For Glib::ConvertError
#include <vector>
#include <iostream>
#include <Logging.h>
#include <stdlib.h>

static Schedule* s_instance = 0;
static Canvas* canv_instance = 0;
int MySaxParser::level = 0;
static bool on_end_elem = false;
static bool have_attribute = false;
bool Schedule::schedule_parsing = false;
bool Canvas::canvas_parsing = false;


std::vector<std::string> nodes;

Schedule * Schedule::instance()
{
    if(!s_instance) {
        s_instance = new Schedule();
    }

    return s_instance;
}

Schedule::Schedule()
    :Zorder(0){

}

Schedule::~Schedule(){

}

Canvas * Canvas::instance()
{
    if(!canv_instance) {
        canv_instance = new Canvas();
    }

    return canv_instance;
}

Canvas::Canvas()
  :lineSpacing(0)
  ,level(0)
  ,bold(false)
  ,italic(false)
  ,text_size(0)
  ,underline(false)
  ,speed(0)
  ,space(0)
  ,line_thickness(0)
  ,window_flag(false)
  ,text_flag(false)
  ,line_flag(false)
  ,line_view(false)
{

}

Canvas::~Canvas(){

}

MySaxParser::MySaxParser()
   :xmlpp::SaxParser()
{
}

MySaxParser::~MySaxParser()
{
}

void MySaxParser::on_start_document()
{
    LOG_DEBUG("on_start_XML_document");
}

void MySaxParser::on_end_document()
{
    LOG_DEBUG("on_end_XML_document");
}

void MySaxParser::on_start_element(const Glib::ustring& name,
                                   const AttributeList& attributes)
{
    nodes.push_back(name);
    pbnjson::JValue Object;

    xmlpp::SaxParser::AttributeList::const_iterator iter = attributes.begin();
    if(iter!= attributes.end())
    {
        Object = pbnjson::Object();
        Object.put("node",name.c_str());
        have_attribute = true;

        for(xmlpp::SaxParser::AttributeList::const_iterator iter = attributes.begin(); iter != attributes.end(); ++iter)
        {

            Object.put((iter->name).c_str(),iter->value.c_str());
        }

        if(!Object.isNull())
        {
            if(Schedule::schedule_parsing)
                Schedule::instance()->process_Schedule_Objects(Object);
            else
                Canvas::instance()->process_Canvas_Objects(Object,level);
        }
        level++;
    }
}

void MySaxParser::on_end_element(const Glib::ustring& /* name */)
{
    LOG_DEBUG("on_end XML element");
    on_end_elem = true;
}

void MySaxParser::on_characters(const Glib::ustring& text)
{
    if(!on_end_elem && !have_attribute)
    {
        //process Key_pair value with Node and level
        std::string key = nodes.back();
        if(Schedule::schedule_parsing)
            Schedule::instance()->process_Schedule_OnCharacter(key,text);
        else
            Canvas::instance()->process_Canvas_OnCharacter(key,text);
    }
    on_end_elem = false;
    have_attribute = false;
}

void MySaxParser::on_comment(const Glib::ustring& text)
{
    LOG_DEBUG("Xml Comment %s",text.c_str());
}

void MySaxParser::on_warning(const Glib::ustring& text)
{
    LOG_DEBUG("Xml warning %s",text.c_str());
}

void MySaxParser::on_error(const Glib::ustring& text)
{
    LOG_DEBUG("Xml ERROR %s",text.c_str());
}

void MySaxParser::on_fatal_error(const Glib::ustring& text)
{
    LOG_DEBUG("Xml FATAL_ERROR %s",text.c_str());
}

//Process Scehdule.ace

void Schedule::process_Schedule_Objects(pbnjson::JValue &Obj)
{

    std::string node;
    node = Obj["node"].asString();

    if (node.compare("Period") == 0)
    {
        Schedule::instance()->Period = pbnjson::Object();
        Schedule::instance()->Period.put("CanvasPath",Obj["CanvasPath"]);
        Schedule::instance()->Period.put("MediaPath",Obj["MediaPath"]);
        Schedule::instance()->Period.put("Type",Obj["Type"]);
    }
    else if(node.compare("Schedule") == 0)
    {
        Schedule::instance()->sched = pbnjson::Object();
        Schedule::instance()->sched.put("Duration",Obj["Duration"]);
        Schedule::instance()->sched.put("Name",Obj["Name"]);
        Schedule::instance()->sched.put("StartDateTime",Obj["StartDateTime"]);
        Schedule::instance()->sched.put("Repeat",Obj["Repeat"]);
        Schedule::instance()->sched.put("Idx",Obj["Idx"]);
    }

}

void Schedule::process_Schedule_OnCharacter(std::string key,std::string value)
{

    if(key.compare("CanvasName") == 0)
    {
        Schedule::instance()->CanvasName = value;
    }
    else if(key.compare("CanvasType") == 0)
    {
        Schedule::instance()->CanvasType = value;
    }
    else if(key.compare("Zorder") == 0)
    {
        Schedule::instance()->Zorder = atoi(value.c_str());
    }
}

//Procees Canvas xml
void Canvas::process_Canvas_Objects(pbnjson::JValue &Obj, int level)
{

    std::string node;
    node = Obj["node"].asString();
    Canvas *canv_Obj = Canvas::instance();
    if (node.compare("Canvas") == 0)
    {
        canv_Obj->canvas = pbnjson::Object();
        canv_Obj->canvas.put("Duration",Obj["Duration"]);
        canv_Obj->canvas.put("Name",Obj["Name"]);
        canv_Obj->canvas.put("Type",Obj["Type"]);
        canv_Obj->canvas.put("Idx",Obj["Idx"]);
    }

    else if(node.compare("Region") == 0)
    {
        if(Obj["Name"].asString().compare("background") == 0)
        {
            canv_Obj->wind_Region = pbnjson::Object();
            canv_Obj->wind_Region.put("Height",Obj["Height"]);
            canv_Obj->wind_Region.put("Name",Obj["Name"]);
            canv_Obj->wind_Region.put("Width",Obj["Width"]);
            canv_Obj->wind_Region.put("X",Obj["X"]);
            canv_Obj->wind_Region.put("Y",Obj["Y"]);
            canv_Obj->wind_Region.put("Z",Obj["Z"]);
        }
        else
        {
            canv_Obj->text_Region = pbnjson::Object();
            canv_Obj->text_Region.put("Height",Obj["Height"]);
            canv_Obj->text_Region.put("Name",Obj["Name"]);
            canv_Obj->text_Region.put("Width",Obj["Width"]);
            canv_Obj->text_Region.put("X",Obj["X"]);
            canv_Obj->text_Region.put("Y",Obj["Y"]);
            canv_Obj->text_Region.put("Z",Obj["Z"]);
        }

    }

    else if(node.compare("Media") == 0)
    {
        if(Obj["Type"].asString().compare("Window") == 0)
        {
            canv_Obj->window_flag = true;
            canv_Obj->text_flag = false;
        }
        else{
            canv_Obj->text_flag = true;
            canv_Obj->window_flag = false;
            canv_Obj->line_flag = false;
        }
    }

    else if(node.compare("Line") == 0)
    {
        canv_Obj->line_view = Obj["View"].asBool();
        canv_Obj->line_flag = true;
        canv_Obj->text_flag = false;
    }

    else if(node.compare("Content") == 0)
    {
        canv_Obj->content = pbnjson ::Object();
        canv_Obj->content.put("Duration",Obj["Duration"]);
        canv_Obj->content.put("StartTime",Obj["StartTime"]);
    }

}

void Canvas::process_Canvas_OnCharacter(std::string key,std::string value)
{
    Canvas *canv_Obj = Canvas::instance();
    if(key.compare("BkColor") == 0 && canv_Obj->window_flag)
        canv_Obj->win_bckGrndclr = value;
    else if(key.compare("BkColor") == 0 && canv_Obj->text_flag)
        canv_Obj->text_bckGrndclr = value;
    else if(key.compare("Color") == 0)
    {
        if(canv_Obj->line_flag)
            canv_Obj->line_color = value;
        else
            canv_Obj->text_color = value;
    }
    else if(key.compare("Thickness") == 0)
        canv_Obj->line_thickness = atoi(value.c_str());
    else if(key.compare("Font") == 0)
        canv_Obj->font = value;
    else if(key.compare("Bold") == 0)
    {
        if(value.compare("true") == 0)
            canv_Obj->bold = true;
        else
            canv_Obj->bold = false;
    }
    else if(key.compare("Italic") == 0)
    {
        if(value.compare("true") == 0)
            canv_Obj->italic = true;
        else
            canv_Obj->italic = false;
    }
    else if(key.compare("Underline") == 0)
    {
        if(value.compare("true") == 0)
            canv_Obj->underline = true;
        else
            canv_Obj->underline = false;
    }
    else if(key.compare("Size") == 0)
        canv_Obj->text_size = atoi(value.c_str());
    else if(key.compare("Space") == 0)
        canv_Obj->space = atoi(value.c_str());
    else if(key.compare("Effect") == 0)
        canv_Obj->effect = value;
    else if(key.compare("Speed") == 0)
        canv_Obj->speed = atoi(value.c_str());
    else if(key.compare("HAlign") == 0)
        canv_Obj->hAlign = value;
    else if(key.compare("VAlign") == 0)
        canv_Obj->vAlign = value;
    else if(key.compare("LineSpace") == 0)
        canv_Obj->lineSpacing = atoi(value.c_str());
    else if(key.compare("String") == 0)
    {
        if ((canv_Obj->message).empty())
        {
            canv_Obj->message = value;
        }
        else
        {
           canv_Obj->message = (canv_Obj->message).append(value);
        }

        size_t pos = 0;
        std::string toReplace = ";nl;";

        while ((pos = canv_Obj->message.find(toReplace)) != std::string::npos)
        {
            std::string replaceWith = "\n";
            canv_Obj->message.replace(pos, toReplace.length(), replaceWith);
            pos += replaceWith.length();
        }
    }
    else if(key.compare("Repeat") == 0)
        canv_Obj->repeat = value;
}
