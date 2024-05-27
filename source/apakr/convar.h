class HackedConVar : public ConCommandBase, public IConVar
{
friend class CCvar;
friend class ConVarRef;

public:
	typedef ConCommandBase BaseClass;

								HackedConVar( const char *pName, const char *pDefaultValue, int flags = 0);

								HackedConVar( const char *pName, const char *pDefaultValue, int flags, 
									const char *pHelpString );
								HackedConVar( const char *pName, const char *pDefaultValue, int flags, 
									const char *pHelpString, bool bMin, float fMin, bool bMax, float fMax );
								HackedConVar( const char *pName, const char *pDefaultValue, int flags, 
									const char *pHelpString, FnChangeCallback_t callback );
								HackedConVar( const char *pName, const char *pDefaultValue, int flags, 
									const char *pHelpString, bool bMin, float fMin, bool bMax, float fMax,
									FnChangeCallback_t callback );

	virtual						~HackedConVar( void );

	virtual bool				IsFlagSet( int flag ) const;
	virtual const char*			GetHelpText( void ) const;
	virtual bool				IsRegistered( void ) const;
	virtual const char			*GetName( void ) const;
	virtual void				AddFlags( int flags );
	virtual	bool				IsCommand( void ) const;

	// Install a change callback (there shouldn't already be one....)
	void InstallChangeCallback( FnChangeCallback_t callback );

	// Retrieve value
	FORCEINLINE_CVAR float			GetFloat( void ) const;
	FORCEINLINE_CVAR int			GetInt( void ) const;
	FORCEINLINE_CVAR bool			GetBool() const {  return !!GetInt(); }
	FORCEINLINE_CVAR char const	   *GetString( void ) const;

	// Any function that allocates/frees memory needs to be virtual or else you'll have crashes
	//  from alloc/free across dll/exe boundaries.
	
	// These just call into the IConCommandBaseAccessor to check flags and set the var (which ends up calling InternalSetValue).
	virtual void				SetValue( const char *value );
	virtual void				SetValue( float value );
	virtual void				SetValue( int value );
	
	// Reset to default value
	void						Revert( void );

	// True if it has a min/max setting
	bool						GetMin( float& minVal ) const;
	bool						GetMax( float& maxVal ) const;
	const char					*GetDefault( void ) const;
	void						SetDefault( const char *pszDefault );

	// Called by CCvar when the value of a var is changing.
	virtual void				InternalSetValue(const char *value);
	// For CVARs marked FCVAR_NEVER_AS_STRING
	virtual void				InternalSetFloatValue( float fNewValue );
	virtual void				InternalSetIntValue( int nValue );

	virtual bool				ClampValue( float& value );
	virtual void				ChangeStringValue( const char *tempVal, float flOldValue );

	virtual void				Create( const char *pName, const char *pDefaultValue, int flags = 0,
									const char *pHelpString = 0, bool bMin = false, float fMin = 0.0,
									bool bMax = false, float fMax = false, FnChangeCallback_t callback = 0 );

	// Needed for Gmod, or else it causes a crash on level Shutdown.
	virtual void				SetServerValue( const char* pName ) {};

	// Used internally by OneTimeInit to initialize.
	virtual void				Init();
	int GetFlags() { return 0; }

	// This either points to "this" or it points to the original declaration of a ConVar.
	// This allows ConVars to exist in separate modules, and they all use the first one to be declared.
	// m_pParent->m_pParent must equal m_pParent (ie: m_pParent must be the root, or original, ConVar).
	HackedConVar						*m_pParent;

	// Static data
	const char					*m_pszDefaultValue;
	
	// Value
	// Dynamically allocated
	char						*m_pszString;
	int							m_StringLength;

	// Values
	float						m_fValue;
	int							m_nValue;

	// Min/Max values
	bool						m_bHasMin;
	float						m_fMinVal;
	bool						m_bHasMax;
	float						m_fMaxVal;
	
	// Call this function when ConVar changes
	FnChangeCallback_t			m_fnChangeCallback;

	CUtlString m_unknown;
};
