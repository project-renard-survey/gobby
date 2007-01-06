/* gobby - A GTKmm driven libobby client
 * Copyright (C) 2005 0x539 dev group
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <glibmm/pattern.h>

#include "preferences.hpp"
#include "docwindow.hpp"

namespace
{
	Gtk::WrapMode wrap_mode_from_preferences(const Gobby::Preferences& pref)
	{
		if(pref.view.wrap_text)
		{
			if(pref.view.wrap_words)
				return Gtk::WRAP_CHAR;
			else
				return Gtk::WRAP_WORD;
		}
		else
		{
			return Gtk::WRAP_NONE;
		}
	}
}

Gobby::DocWindow::DocWindow(LocalDocumentInfo& info,
                            const Preferences& preferences):
	m_info(info), m_doc(info.get_content() ),
	m_preferences(preferences), /*m_editing(false),*/
	m_title(info.get_title() ),
	m_scrolly(0.0),
	m_scroll_restore(false)
{
	if(!info.is_subscribed() )
	{
		throw std::logic_error(
			"Gobby::DocWindow::DocWindow:\n"
			"Local user is not subscribed"
		);
	}

	Glib::RefPtr<Gtk::SourceBuffer> buf = m_doc.get_buffer();
	m_view.set_buffer(buf);

	// Set source language by filename
	buf->set_highlight(false);

	for(Preferences::FileList::iterator iter = preferences.files.begin();
	    iter != preferences.files.end();
	    ++ iter)
	{
		Glib::PatternSpec spec(iter.pattern());
		if(spec.match(info.get_title()) )
		{
			buf->set_language(iter.language() );
			buf->set_highlight(true);
		}
	}

	buf->signal_mark_set().connect(
		sigc::mem_fun(*this, &DocWindow::on_mark_set)
	);

	buf->signal_changed().connect(
		sigc::mem_fun(*this, &DocWindow::on_changed)
	);

	m_doc.local_insert_event().connect(
		sigc::mem_fun(*this, &DocWindow::on_local_insert)
	);

	m_doc.local_erase_event().connect(
		sigc::mem_fun(*this, &DocWindow::on_local_erase)
	);

	m_doc.remote_insert_before_event().connect(
		sigc::mem_fun(*this, &DocWindow::on_remote_insert_before)
	);

	m_doc.remote_erase_before_event().connect(
		sigc::mem_fun(*this, &DocWindow::on_remote_erase_before)
	);

	m_doc.remote_insert_after_event().connect(
		sigc::mem_fun(*this, &DocWindow::on_remote_insert_after)
	);

	m_doc.remote_erase_after_event().connect(
		sigc::mem_fun(*this, &DocWindow::on_remote_erase_after)
	);

	apply_preferences();
	buf->set_modified(!m_doc.empty() );

	set_shadow_type(Gtk::SHADOW_IN);
	set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
	add(m_view);
}

void Gobby::DocWindow::get_cursor_position(unsigned int& row,
                                           unsigned int& col)
{
	Glib::RefPtr<Gtk::TextMark> mark = m_view.get_buffer()->get_insert();

        // Gtk::TextBuffer::Mark::get_iter is not const. Why not? It prevents
	// this function from being const.
	Gtk::TextBuffer::iterator iter = mark->get_iter();

	// Row is trivial
	row = iter.get_line(); col = 0;
	int chars = iter.get_line_offset();

	// Tab characters expand to more than one column
	unsigned int tabs = m_preferences.editor.tab_width;
	for(iter.set_line_offset(0); iter.get_line_offset() < chars; ++ iter)
	{
		unsigned int width = 1;
		if(*iter == '\t')
		{
			width = (tabs - iter.get_line_offset() % tabs) % tabs;
			if(width == 0) width = tabs;
		}

		col += width;
	}
}

void Gobby::DocWindow::set_selection(const Gtk::TextIter& begin,
                                     const Gtk::TextIter& end)
{
	m_view.get_buffer()->select_range(begin, end);
	m_view.scroll_to(m_view.get_buffer()->get_insert(), 0.1);
}

void Gobby::DocWindow::disable()
{
	m_view.set_sensitive(false);
}

Glib::ustring Gobby::DocWindow::get_selected_text() const
{
	Gtk::TextIter start, end;
	m_view.get_buffer()->get_selection_bounds(start, end);
	return start.get_slice(end);
}

const Glib::ustring& Gobby::DocWindow::get_title() const
{
	return m_title;
}

bool Gobby::DocWindow::get_modified() const
{
	return m_view.get_buffer()->get_modified();
}

void Gobby::DocWindow::grab_focus()
{
	Gtk::ScrolledWindow::grab_focus();
	m_view.grab_focus();
}

Glib::RefPtr<Gtk::SourceLanguage> Gobby::DocWindow::get_language() const
{
	return m_view.get_buffer()->get_language();
}

void Gobby::DocWindow::
	set_language(const Glib::RefPtr<Gtk::SourceLanguage>& language)
{
	m_view.get_buffer()->set_language(language);
	m_view.get_buffer()->set_highlight(language);
	m_signal_language_changed.emit();
}

const Gobby::Preferences& Gobby::DocWindow::get_preferences() const
{
	return m_preferences;
}

void Gobby::DocWindow::set_preferences(const Preferences& preferences)
{
	m_preferences = preferences;
	apply_preferences();
}

Glib::ustring Gobby::DocWindow::get_content() const
{
	// Why is Gtk::TextBuffer::get_text not const!?
	return const_cast<Gtk::SourceView&>(m_view).get_buffer()->get_text();
}

Gobby::DocWindow::signal_cursor_moved_type
Gobby::DocWindow::cursor_moved_event() const
{
	return m_signal_cursor_moved;
}

Gobby::DocWindow::signal_content_changed_type
Gobby::DocWindow::content_changed_event() const
{
	return m_signal_content_changed;
}

Gobby::DocWindow::signal_language_changed_type
Gobby::DocWindow::language_changed_event() const
{
	return m_signal_language_changed;
}

const Gobby::LocalDocumentInfo& Gobby::DocWindow::get_info() const
{
	return m_info;
}

Gobby::LocalDocumentInfo& Gobby::DocWindow::get_info()
{
	return m_info;
}

const Gobby::Document& Gobby::DocWindow::get_document() const
{
	return m_doc;
}

void Gobby::DocWindow::on_mark_set(const Gtk::TextIter& location,
                                   const Glib::RefPtr<Gtk::TextMark>& mark)
{
	// Mark was deleted
	if(!mark) return;

	if(mark == m_view.get_buffer()->get_insert() )
		m_signal_cursor_moved.emit();
}

void Gobby::DocWindow::on_changed()
{
	// Cursor may have moved.
	// TODO: Check if the cursor really moved
	m_signal_cursor_moved.emit();
	m_signal_content_changed.emit();
}

void Gobby::DocWindow::on_local_insert(obby::position pos,
                                       const std::string& error)
{
	m_info.insert(pos, error);
}

void Gobby::DocWindow::on_local_erase(obby::position pos,
                                      obby::position len)
{
	m_info.erase(pos, len);
}

void Gobby::DocWindow::on_remote_insert_before(obby::position,
                                               const std::string& text)
{
	store_scroll();
}

void Gobby::DocWindow::on_remote_erase_before(obby::position pos,
                                              obby::position len)
{
	store_scroll();
}

void Gobby::DocWindow::on_remote_insert_after(obby::position,
                                              const std::string& text)
{
	restore_scroll();
}

void Gobby::DocWindow::on_remote_erase_after(obby::position pos,
                                             obby::position len)
{
	restore_scroll();
}

void Gobby::DocWindow::apply_preferences()
{
	m_view.set_tabs_width(m_preferences.editor.tab_width);
	m_view.set_insert_spaces_instead_of_tabs(
		m_preferences.editor.tab_spaces
	);
	m_view.set_auto_indent(m_preferences.editor.indentation_auto);
	m_view.set_smart_home_end(m_preferences.editor.homeend_smart);

	m_view.set_wrap_mode(wrap_mode_from_preferences(m_preferences) );
	m_view.set_show_line_numbers(m_preferences.view.linenum_display);
	m_view.set_highlight_current_line(m_preferences.view.curline_highlight);
	m_view.set_show_margin(m_preferences.view.margin_display);
	m_view.set_margin(m_preferences.view.margin_pos);
	m_view.get_buffer()->set_check_brackets(
		m_preferences.view.bracket_highlight
	);

	m_view.modify_font(m_preferences.font.desc);

	// Cursor position may have changed because of new tab width
	// TODO: Only emit if the position really changed
	m_signal_cursor_moved.emit();
}

void Gobby::DocWindow::store_scroll()
{
	Gdk::Rectangle curs_rect;
	int x, y;

	m_view.get_iter_location(m_view.get_buffer()->get_insert()->get_iter(), curs_rect);
	m_view.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, curs_rect.get_x(), curs_rect.get_y(), x, y);

	m_scrolly = y / static_cast<double>((m_view.get_height() - curs_rect.get_height()));
	m_scroll_restore = true;
}

void Gobby::DocWindow::restore_scroll()
{
	if(m_scroll_restore)
	{
		if(m_scrolly >= 0.0 && m_scrolly <= 1.0)
			m_view.scroll_to(m_view.get_buffer()->get_insert(), 0, 0, m_scrolly);

		m_scroll_restore = false;
	}
}
