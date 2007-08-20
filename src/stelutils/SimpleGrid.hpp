/*
 * Stellarium
 * Copyright (C) 2007 Guillaume Chereau
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
 
 
#ifndef _SIMPLEGRID_HPP_
#define _SIMPLEGRID_HPP_

#include <list>

#include "Grid.hpp"

class SimpleGrid : public Grid
{
public:
    SimpleGrid() {}
    
    ~SimpleGrid() {}
	void insert(GridObject* obj)
    {
        all.push_back(obj);
    }

	template<class Shape>
	void filterIntersect(const Shape& s);

	//! Get all the object loaded into the grid
	std::vector<GridObject*>& getAllObjects() {return all;}
private:
	typedef std::vector<GridObject*> AllObjects;
    AllObjects all;
};

template<class Shape>
void SimpleGrid::filterIntersect(const Shape& s)
{
	// This is a totaly non-optimized method
	// because we recompute everything each time, when we
	// should use the previous informations we have
	this->clear();
	for (AllObjects::iterator iter = all.begin(); iter != all.end(); ++iter)
	{
		if (intersect(s, (*iter)->getPositionForGrid()))
		{
			this->Grid::insert(*iter);
		}
	}
}
			
#endif // _SIMPLEGRID_HPP_
 
