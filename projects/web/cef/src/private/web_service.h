#pragma once
#ifndef PRIVATE_WEBSERVICE_H_
#define PRIVATE_WEBSERVICE_H_

#include <ds/app/engine/engine_service.h>
#include <ds/app/auto_update.h>

namespace ds {
class Engine;

namespace web {

/**
 * \class ds::web::Service
 * \brief The engine service object that provides access to the
 * web service.
 */
class Service : public ds::EngineService,
				public ds::AutoUpdate {
public:
	Service(ds::Engine&);
	~Service();

	virtual void			start();

protected:
	virtual void			update(const ds::UpdateParams&);

private:
};

} // namespace web
} // namespace ds

#endif // PRIVATE_WEBSERVICE_H_