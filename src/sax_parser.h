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

#ifndef __LIBXMLPP_MYPARSER_H
#define __LIBXMLPP_MYPARSER_H

#include <libxml++/libxml++.h>
#include <pbnjson.hpp>

class MySaxParser : public xmlpp::SaxParser
{
public:
  MySaxParser();
  virtual ~MySaxParser();
  static int level; //To know the order of parsing

protected:
  //overrides:
  virtual void on_start_document();
  virtual void on_end_document();
  virtual void on_start_element(const Glib::ustring& name,
                                const AttributeList& properties);
  virtual void on_end_element(const Glib::ustring& name);
  virtual void on_characters(const Glib::ustring& characters);
  virtual void on_comment(const Glib::ustring& text);
  virtual void on_warning(const Glib::ustring& text);
  virtual void on_error(const Glib::ustring& text);
  virtual void on_fatal_error(const Glib::ustring& text);
};

class Schedule
{
public:
    Schedule();
    ~Schedule();
    static Schedule *instance();
    void process_Schedule_Objects(pbnjson::JValue &Obj);
    void process_Schedule_OnCharacter(std::string key,std::string value);
    pbnjson::JValue Period;
    pbnjson::JValue sched;
    std::string CanvasName;
    std::string CanvasPath;
    std::string CanvasType;
    int Zorder;
    static bool schedule_parsing;
};

class Canvas
{
public:
    Canvas();
    ~Canvas();
    static Canvas *instance();
    void process_Canvas_Objects(pbnjson::JValue &Obj, int level);
    void process_Canvas_OnCharacter(std::string key,std::string value);
    int level;
    pbnjson::JValue canvas;
    pbnjson::JValue wind_Region;
    pbnjson::JValue content;
    pbnjson::JValue text_Region;
    std::string hAlign;
    std::string vAlign;
    int lineSpacing;
    std::string win_bckGrndclr;
    std::string message;
    std::string repeat;
    std::string text_bckGrndclr;
    std::string font;
    bool bold;
    std::string text_color;
    bool italic;
    int text_size;
    bool underline;
    int speed;
    int space;
    std::string line_color;
    int line_thickness;
    bool window_flag;
    bool text_flag;
    bool line_flag;
    bool line_view;
    std::string effect;
    static bool canvas_parsing;
};

#endif //__LIBXMLPP_MYPARSER_H
