/*
 * Stellarium Remote Control plugin
 * Copyright (C) 2015 Florian Schaukowitsch
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
 * Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA  02110-1335, USA.
 */

#include "MainService.hpp"

#include "StelApp.hpp"
#include "StelActionMgr.hpp"
#include "StelCore.hpp"
#include "LandscapeMgr.hpp"
#include "StelLocaleMgr.hpp"
#include "StelMainView.hpp"
#include "StelModuleMgr.hpp"
#include "StelMovementMgr.hpp"
#include "StelObjectMgr.hpp"
#include "StelPropertyMgr.hpp"
#include "StelScriptMgr.hpp"
#include "StelSkyCultureMgr.hpp"
#include "StelTranslator.hpp"
#include "StelUtils.hpp"

#include <QJsonDocument>


MainService::MainService(const QByteArray &serviceName, QObject *parent)
	: AbstractAPIService(serviceName,parent),
	  moveX(0),moveY(0),lastMoveUpdateTime(0)
{
	//100 should be more than enough
	//this only has to emcompass events that occur between 2 status updates
	actionCache.setCapacity(100);
	propCache.setCapacity(100);

	//this is run in the main thread
	core = StelApp::getInstance().getCore();
	actionMgr =  StelApp::getInstance().getStelActionManager();
	lsMgr = GETSTELMODULE(LandscapeMgr);
	localeMgr = &StelApp::getInstance().getLocaleMgr();
	objMgr = &StelApp::getInstance().getStelObjectMgr();
	mvmgr = GETSTELMODULE(StelMovementMgr);
	propMgr = StelApp::getInstance().getStelPropertyManager();
	scriptMgr = &StelApp::getInstance().getScriptMgr();
	skyCulMgr = &StelApp::getInstance().getSkyCultureMgr();

	connect(actionMgr,SIGNAL(actionToggled(QString,bool)),this,SLOT(actionToggled(QString,bool)));
	connect(propMgr,SIGNAL(stelPropChanged(QString,QVariant)),this,SLOT(propertyChanged(QString,QVariant)));

	Q_ASSERT(this->thread()==objMgr->thread());
}

void MainService::update(double deltaTime)
{
	bool xZero = qFuzzyIsNull(moveX);
	bool yZero = qFuzzyIsNull(moveY);

	//prevent sudden disconnects from moving endlessly
	if((QDateTime::currentMSecsSinceEpoch() - lastMoveUpdateTime) > 1000)
	{
		if(!xZero || !yZero)
			qDebug()<<"[MainService] move timeout";
		moveX = moveY = .0f;
	}

	//Similar to StelMovementMgr::updateMotion

	if(!xZero || !yZero)
	{
		double currentFov = mvmgr->getCurrentFov();
		// the more it is zoomed, the lower the moving speed is (in angle)
		double depl=0.00025*deltaTime*1000*currentFov;

		float deltaAz = moveX;
		float deltaAlt = moveY;

		if (deltaAz<0)
		{
			deltaAz = -depl/30;
			if (deltaAz<-0.2)
				deltaAz = -0.2;
		}
		else
		{
			if (deltaAz>0)
			{
				deltaAz = (depl/30);
				if (deltaAz>0.2)
					deltaAz = 0.2;
			}
		}

		if (deltaAlt<0)
		{
			deltaAlt = -depl/30;
			if (deltaAlt<-0.2)
				deltaAlt = -0.2;
		}
		else
		{
			if (deltaAlt>0)
			{
				deltaAlt = depl/30;
				if (deltaAlt>0.2)
					deltaAlt = 0.2;
			}
		}

		mvmgr->panView(deltaAz,deltaAlt);

		//this is required to enable maximal fps for smoothness
		StelMainView::getInstance().thereWasAnEvent();
	}
}

void MainService::getImpl(const QByteArray& operation, const APIParameters &parameters, APIServiceResponse &response)
{
	if(operation=="status")
	{
		//a listing of the most common stuff that can change often

		QString sActionId = QString::fromUtf8(parameters.value("actionId"));
		bool actionOk;
		int actionId = sActionId.toInt(&actionOk);

		QString sPropId = QString::fromUtf8(parameters.value("propId"));
		bool propOk;
		int propId = sPropId.toInt(&propOk);

		QJsonObject obj;

		//// Location
		const StelLocation& loc = core->getCurrentLocation();
		{
			QJsonObject obj2;
			obj2.insert("name",loc.name);
			obj2.insert("role",QString(loc.role));
			obj2.insert("planet",loc.planetName);
			obj2.insert("latitude",loc.latitude);
			obj2.insert("longitude",loc.longitude);
			obj2.insert("altitude",loc.altitude);
			obj2.insert("country",loc.country);
			obj2.insert("state",loc.state);
			obj2.insert("landscapeKey",loc.landscapeKey);
			obj.insert("location",obj2);
		}

		//// Time related stuff
		{
			double jday = core->getJD();
			double deltaT = core->getDeltaT() * StelCore::JD_SECOND;

			double gmtShift = core->getUTCOffset(jday) / 24.0;

			QString utcIso = StelUtils::julianDayToISO8601String(jday,true).append('Z');
			QString localIso = StelUtils::julianDayToISO8601String(jday+gmtShift,true);

			//time zone string
			QString timeZone = localeMgr->getPrintableTimeZoneLocal(jday);

			QJsonObject obj2;
			obj2.insert("jday",jday);
			obj2.insert("deltaT",deltaT);
			obj2.insert("gmtShift",gmtShift);
			obj2.insert("timeZone",timeZone);
			obj2.insert("utc",utcIso);
			obj2.insert("local",localIso);
			obj2.insert("isTimeNow",core->getIsTimeNow());
			obj2.insert("timerate",core->getTimeRate());
			obj.insert("time",obj2);
		}

		//// Info about selected object (only primary)
		{
			QString infoStr;
			QMetaObject::invokeMethod(this,"getInfoString",SERVICE_DEFAULT_INVOKETYPE,
						  Q_RETURN_ARG(QString,infoStr));
			obj.insert("selectioninfo",infoStr);
		}

		//// Info about current view
		{
			QJsonObject obj2;

			// the aim fov may lie outside the min/max bounds, so constrain it
			double fov = mvmgr->getAimFov();
			if(fov < mvmgr->getMinFov())
				fov = mvmgr->getMinFov();
			else if (fov>mvmgr->getMaxFov())
				fov = mvmgr->getMaxFov();

			obj2.insert("fov",fov);

			obj.insert("view",obj2);
		}

		//// Info about changed actions & props (if requested)
		{
			if(actionOk)
				obj.insert("actionChanges",getActionChangesSinceID(actionId));
			if(propOk)
				obj.insert("propertyChanges",getPropertyChangesSinceID(propId));
		}

		response.writeJSON(QJsonDocument(obj));
	}
	else if(operation=="plugins")
	{
		// Retrieve list of plugins

		QJsonObject mainObj;

		StelModuleMgr& modMgr = StelApp::getInstance().getModuleMgr();
		foreach(const StelModuleMgr::PluginDescriptor& desc, modMgr.getPluginsList())
		{
			QJsonObject pluginObj,infoObj;
			pluginObj.insert("loadAtStartup", desc.loadAtStartup);
			pluginObj.insert("loaded", desc.loaded);

			infoObj.insert("authors", desc.info.authors);
			infoObj.insert("contact", desc.info.contact);
			infoObj.insert("description", desc.info.description);
			infoObj.insert("displayedName", desc.info.displayedName);
			infoObj.insert("startByDefault", desc.info.startByDefault);
			infoObj.insert("version", desc.info.version);

			pluginObj.insert("info",infoObj);
			mainObj.insert(desc.info.id, pluginObj);
		}

		response.writeJSON(QJsonDocument(mainObj));
	}
	else
	{
		//TODO some sort of service description?
		response.writeRequestError("unsupported operation. GET: status, plugins");
	}
}

void MainService::postImpl(const QByteArray& operation, const APIParameters &parameters, const QByteArray &data, APIServiceResponse &response)
{
	Q_UNUSED(data);

	if(operation == "time")
	{
		bool doneSomething = false;
		bool ok;

		//set the time + timerate
		{
			const QByteArray& raw = parameters.value("time");
			if(!raw.isEmpty())
			{
				//parse time and set it
				double jday = QString(raw).toDouble(&ok);
				if(ok)
				{
					//check for invalid double (NaN, inf...)
					//this will crash the app if it is allowed
					if(qIsNaN(jday) || qIsInf(jday))
					{
						qWarning()<<"[RemoteControl] Prevented setting invalid time"<<jday<<", does the web interface have a bug?";
						response.setData("error: invalid time value");
						return;
					}

					doneSomething = true;
					//set new time
					QMetaObject::invokeMethod(core,"setJD", SERVICE_DEFAULT_INVOKETYPE,
								  Q_ARG(double,jday));
				}
			}
		}
		{
			const QByteArray& raw = parameters.value("timerate");
			if(!raw.isEmpty())
			{
				//parse timerate and set it
				double rate = QString(raw).toDouble(&ok);
				if(ok)
				{
					doneSomething = true;
					//set new time rate
					QMetaObject::invokeMethod(core,"setTimeRate", SERVICE_DEFAULT_INVOKETYPE,
								  Q_ARG(double,rate));
				}
			}
		}

		if(doneSomething)
			response.setData("ok");
		else
			response.setData("error: invalid parameters, use time/timerate as double values");
	}
	else if(operation == "focus")
	{
		QString target = QString::fromUtf8(parameters.value("target"));

		//check target string first
		if(target.isEmpty())
		{
			if(parameters.value("position").isEmpty())
			{
				//no parameters = clear focus
				target = "";
			}
			else
			{
				//parse position
				QJsonDocument doc = QJsonDocument::fromJson(parameters.value("position"));
				QJsonArray arr = doc.array();
				if(arr.size() == 3)
				{
					Vec3d pos;
					pos[0] = arr.at(0).toDouble();
					pos[1] = arr.at(1).toDouble();
					pos[2] = arr.at(2).toDouble();

					//deselect and move
					QMetaObject::invokeMethod(this,"focusPosition", SERVICE_DEFAULT_INVOKETYPE,
								  Q_ARG(Vec3d,pos));
					response.setData("ok");
					return;
				}

				response.writeRequestError("invalid position format");
				return;
			}
		}

		bool result;
		QMetaObject::invokeMethod(this,"focusObject",SERVICE_DEFAULT_INVOKETYPE,
					  Q_RETURN_ARG(bool,result),
					  Q_ARG(QString,target));

		response.setData(result ? "true" : "false");
	}
	else if(operation == "move")
	{
		QString xs = QString::fromUtf8(parameters.value("x"));
		QString ys = QString::fromUtf8(parameters.value("y"));

		bool xOk,yOk;
		float x = xs.toInt(&xOk);
		float y = ys.toInt(&yOk);

		if(xOk || yOk)
		{
			QMetaObject::invokeMethod(this,"updateMovement", SERVICE_DEFAULT_INVOKETYPE,
						  Q_ARG(float,x),
						  Q_ARG(float,y),
						  Q_ARG(bool,xOk),
						  Q_ARG(bool,yOk));

			response.setData("ok");
		}
		else
			response.writeRequestError("requires x or y parameter");
	}
	else if (operation == "fov")
	{
		QString fov = QString::fromUtf8(parameters.value("fov"));
		bool ok;
		double dFov = fov.toDouble(&ok);

		if(fov.isEmpty() || !ok)
		{
			response.writeRequestError("requires fov parameter");
			return;
		}

		QMetaObject::invokeMethod(this,"setFov",SERVICE_DEFAULT_INVOKETYPE,
					  Q_ARG(double,dFov));

		response.setData("ok");
	}
	else
	{
		//TODO some sort of service description?
		response.writeRequestError("unsupported operation. POST: time,focus,move,fov");
	}
}

StelObjectP MainService::getSelectedObject()
{
	const QList<StelObjectP>& list = objMgr->getSelectedObject();
	if(list.isEmpty())
		return StelObjectP();
	return list.first();
}

QString MainService::getInfoString()
{
	StelObjectP selectedObject = getSelectedObject();
	if(selectedObject.isNull())
		return QString();
	return selectedObject->getInfoString(core,StelObject::AllInfo | StelObject::NoFont);
}

bool MainService::focusObject(const QString &name)
{
	//StelDialog::gotoObject

	//if name is empty, unselect
	if(name.isEmpty())
	{
		objMgr->unSelect();
		return true;
	}

	bool result = false;
	if (objMgr->findAndSelectI18n(name) || objMgr->findAndSelect(name))
	{
		const QList<StelObjectP> newSelected = objMgr->getSelectedObject();
		if (!newSelected.empty())
		{
			// Can't point to home planet
			if (newSelected[0]->getEnglishName()!=core->getCurrentLocation().planetName)
			{
				mvmgr->moveToObject(newSelected[0], mvmgr->getAutoMoveDuration());
				mvmgr->setFlagTracking(true);
				result = true;
			}
			else
			{
				objMgr->unSelect();
			}
		}
	}
	return result;
}

void MainService::focusPosition(const Vec3d &pos)
{
	objMgr->unSelect();
	mvmgr->moveToJ2000(pos, mvmgr->mountFrameToJ2000(Vec3d(0., 0., 1.)), mvmgr->getAutoMoveDuration());
}

void MainService::updateMovement(float x, float y, bool xUpdated, bool yUpdated)
{
	if(xUpdated)
	{
		this->moveX = x;
	}
	if(yUpdated)
	{
		this->moveY = y;
	}
	qint64 curTime = QDateTime::currentMSecsSinceEpoch();
	//qDebug()<<"updateMove"<<x<<y<<(curTime-lastMoveUpdateTime);
	lastMoveUpdateTime = curTime;
}

void MainService::setFov(double fov)
{
	//TODO calculate a better move duration here
	mvmgr->zoomTo(fov,0.25f);
}

void MainService::actionToggled(const QString &id, bool val)
{
	actionMutex.lock();
	actionCache.append(ActionCacheEntry(id,val));
	if(!actionCache.areIndexesValid())
	{
		//in theory, this can happen, but practically not so much
		qWarning()<<"Action cache indices invalid";
		actionCache.clear();
	}
	actionMutex.unlock();
}

void MainService::propertyChanged(const QString &id, const QVariant &val)
{
	propMutex.lock();
	propCache.append(PropertyCacheEntry(id,val));
	if(!propCache.areIndexesValid())
	{
		//in theory, this can happen, but practically not so much
		qWarning()<<"Property cache indices invalid";
		propCache.clear();
	}
	propMutex.unlock();
}

QJsonObject MainService::getActionChangesSinceID(int changeId)
{
	//changeId is the last id the interface is available
	//or -2 if the interface just started
	// -1 means the initial state was set

	QJsonObject obj;
	QJsonObject changes;
	int newId = changeId;


	actionMutex.lock();
	if(actionCache.isEmpty())
	{
		if(changeId!=-1)
		{
			//this is either the initial state (-2) or
			//something is "broken", probably from an existing web interface that reconnected after restart
			//force a full reload

			foreach(StelAction* ac, actionMgr->getActionList())
			{
				if(ac->isCheckable())
				{
					changes.insert(ac->getId(),ac->isChecked());
				}
			}
			newId = -1;
		}
	}
	else
	{
		if(changeId > actionCache.lastIndex() || changeId < (actionCache.firstIndex()-1))
		{
			//this is either the initial state (-2) or
			//"broken" state again, force full reload
			foreach(StelAction* ac, actionMgr->getActionList())
			{
				if(ac->isCheckable())
				{
					changes.insert(ac->getId(),ac->isChecked());
				}
			}
			newId = actionCache.lastIndex();
		}
		else if(changeId < actionCache.lastIndex())
		{
			//create a "diff" between changeId to lastIndex
			for(int i = changeId+1;i<=actionCache.lastIndex();++i)
			{
				const ActionCacheEntry& e = actionCache.at(i);
				changes.insert(e.action,e.val);
			}
			newId = actionCache.lastIndex();
		}
		//else no changes happened, interface is at current state!
	}
	actionMutex.unlock();

	obj.insert("changes",changes);
	obj.insert("id",newId);

	return obj;
}

QJsonObject MainService::getPropertyChangesSinceID(int changeId)
{
	//changeId is the last id the interface is available
	//or -2 if the interface just started
	// -1 means the initial state was set
	QJsonObject obj;
	QJsonObject changes;
	int newId = changeId;

	propMutex.lock();
	if(propCache.isEmpty())
	{
		if(changeId!=-1)
		{
			//this is either the initial state (-2) or
			//something is "broken", probably from an existing web interface that reconnected after restart
			//force a full reload
			const StelPropertyMgr::StelPropertyMap& map = propMgr->getPropertyMap();
			for(StelPropertyMgr::StelPropertyMap::const_iterator it = map.constBegin();
			    it!=map.constEnd();++it)
			{
				changes.insert(it.key(), QJsonValue::fromVariant((*it)->getValue()));
			}
			newId = -1;
		}
	}
	else
	{
		if(changeId > propCache.lastIndex() || changeId < (propCache.firstIndex()-1))
		{
			//this is either the initial state (-2) or
			//"broken" state again, force full reload
			const StelPropertyMgr::StelPropertyMap& map = propMgr->getPropertyMap();
			for(StelPropertyMgr::StelPropertyMap::const_iterator it = map.constBegin();
			    it!=map.constEnd();++it)
			{
				changes.insert(it.key(), QJsonValue::fromVariant((*it)->getValue()));
			}
			newId = propCache.lastIndex();
		}
		else if(changeId < propCache.lastIndex())
		{
			//create a "diff" between changeId to lastIndex
			for(int i = changeId+1;i<=propCache.lastIndex();++i)
			{
				const PropertyCacheEntry& e = propCache.at(i);
				changes.insert(e.id,QJsonValue::fromVariant(e.val));
			}
			newId = propCache.lastIndex();
		}
		//else no changes happened, interface is at current state!
	}
	propMutex.unlock();

	obj.insert("changes",changes);
	obj.insert("id",newId);

	return obj;
}
