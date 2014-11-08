/*
History
=======


Notes
=======

*/

#ifndef REGION_H
#define REGION_H

#include "poltype.h"
#include "proplist.h"
#include "zone.h"

#include <map>
#include <string>

namespace Pol {
  namespace Bscript {
	class BObjectImp;
  }
  namespace Core {
	class RegionGroupBase;

	class Region
	{
	public:
	  Bscript::BObjectImp* get_region_string( const std::string& propname );
	  const std::string& name() const;
	  RegionId regionid() const;

	protected:
	  explicit Region( Clib::ConfigElem& elem, RegionId id );
	  virtual ~Region();

	  // virtual void read_config( ConfigElem& elem );
	  void read_custom_config( Clib::ConfigElem& elem );

	  friend class RegionGroupBase;

	protected:
	  std::string name_;
	  RegionId regionid_;
	  //
	  // custom per-region properties
	  //
	  // Note, these aren't "script-packed", they're just strings.
	  //
	  PropertyList proplist_;
	};

	inline const std::string& Region::name() const
	{
	  return name_;
	}

	inline RegionId Region::regionid() const
	{
	  return regionid_;
	}

	class RegionGroupBase
	{
	public:
	  explicit RegionGroupBase( const char* name );
	  virtual ~RegionGroupBase();

	  void read_region( Clib::ConfigElem& elem );
	  void create_bgnd_region( Clib::ConfigElem& elem );

	  const std::string& name() const;
	protected:
	  Region* getregion_byname( const std::string& regionname );
	  Region* getregion_byloc( xcoord x, ycoord y, Plib::Realm* realm );

	  std::vector<Region*> regions_;
	  //RegionId **regionidx_;//[ZONE_X][ZONE_Y]; // 1-based

	  typedef std::map<Plib::Realm*, RegionId**> RegionRealms;
	  RegionRealms regionrealms;
	private:
	  virtual Region* create_region( Clib::ConfigElem& elem, RegionId id ) const = 0;

	  RegionId getregionid( xcoord x, ycoord y, Plib::Realm* realm );
	  void paint_zones( Clib::ConfigElem& elem, RegionId ridx );
	  std::string name_;
	  typedef std::map<std::string, Region*> RegionsByName;
	  RegionsByName regions_byname_;
	};

	inline const std::string& RegionGroupBase::name() const
	{
	  return name_;
	}

	template <class T>
	class RegionGroup : public RegionGroupBase
	{
	public:
	  explicit RegionGroup( const char* name );

	  virtual T* getregion( xcoord x, ycoord y, Plib::Realm* realm );
	  virtual T* getregion( const std::string& regionname );

	protected:
	  virtual Region* create_region( Clib::ConfigElem& elem, RegionId id ) const;


	};

	template<class T>
	RegionGroup<T>::RegionGroup( const char* name ) :
	  RegionGroupBase( name )
	{}

	template<class T>
	inline T* RegionGroup<T>::getregion( xcoord x, ycoord y, Plib::Realm* realm )
	{
	  return static_cast<T*>( getregion_byloc( x, y, realm ) );
	}

	template<class T>
	inline T* RegionGroup<T>::getregion( const std::string& regionname )
	{
	  return static_cast<T*>( getregion_byname( regionname ) );
	}

	template<class T>
	inline Region* RegionGroup<T>::create_region( Clib::ConfigElem& elem, RegionId id ) const
	{
	  return new T( elem, id );
	}

	void read_region_data( RegionGroupBase& grp,
						   const char* preferred_filename,
						   const char* other_filename,
						   const char* tags_expected );
  }
}
#endif
