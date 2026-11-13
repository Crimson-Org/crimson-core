// Builds the script uBlock Origin would run on a YouTube site, such as www.youtube.com, from its filter lists and its
// scriptlet library, which the app downloads as they are published. buildYouTubeRules(files, host) takes each file's
// text by its path in uBlock's repositories, such as src/js/resources/base.js or filters/filters.txt, and returns the
// script with how many rules it holds, or throws when the library or the lists changed beyond what it knows.
var buildYouTubeRules = (() => {
	const LIBRARY = 'src/js/'
	const ENTRY = LIBRARY + 'resources/scriptlets.js'
	const BASE = LIBRARY + 'resources/base.js'
	const PARSER = LIBRARY + 'arglist-parser.js'
	const LIST_PREFIX = 'filters/'
	// A rule names its sites as hosts, which also cover their subdomains, or as entities such as youtube.*
	const ENTITY_ENDING = '.*'
	const EXCLUDED = '~'
	const RULE = /^([^#]*)(##|#@#)\+js\((.*)\)$/
	const SCRIPT_EXTENSION = /\.js$/
	const ARGUMENT_SEPARATOR = ','


	// A module's path from the one that imports it, such as ../jsonpath.js from src/js/resources/shared.js
	function resolve(from, path) {
		const parts = from.split('/').slice(0, -1)
		for (const part of path.split('/')) {
			if (part === '..')
				parts.pop()
			else if (part !== '.')
				parts.push(part)
		}
		return parts.join('/')
	}


	// Turns an ES module into a function of its own, with its imports read from the modules it names and its exports
	// set once its body has run, which suits uBlock's library, whose modules never import each other in a circle
	function wrapModule(path, text) {
		const exported = []
		const body = text
			.replace(/^\s*import\s+'([^']+)';/gm, (all, from) => `require(${JSON.stringify(resolve(path, from))});`)
			.replace(/^\s*import\s*\{([^}]*)\}\s*from\s*'([^']+)';/gm, (all, names, from) =>
				`const {${names.replace(/\s+as\s+/g, ': ')}} = require(${JSON.stringify(resolve(path, from))});`)
			.replace(/^export\s+(function|class|const)\s+([\w$]+)/gm, (all, kind, name) => {
				exported.push(name)
				return `${kind} ${name}`
			})
		if (/^\s*(import|export)\s/m.test(body))
			throw new Error(`${path} uses a module form the rule builder does not know`)
		const assignments = exported.map(name => `exports.${name} = ${name};`).join('\n')
		return new Function('require', 'exports', `${body}\n${assignments}`)
	}


	function loadLibrary(files) {
		const loaded = new Map()
		const require = path => {
			if (!loaded.has(path)) {
				if (!(path in files))
					throw new Error(`The rule builder lacks ${path}`)
				const exports = {}
				loaded.set(path, exports)
				wrapModule(path, files[path])(require, exports)
			}
			return loaded.get(path)
		}
		require(ENTRY)
		return {scriptlets: require(BASE).registeredScriptlets, ArglistParser: require(PARSER).ArglistParser}
	}


	// Arguments split at commas as uBlock splits them, with quotes and escaped commas kept whole
	function readArguments(parser, text) {
		const values = []
		for (let index = 0; ;) {
			const details = parser.nextArg(text, index)
			const value = text.slice(details.argBeg, details.argEnd)
			values.push(details.transform ? parser.normalizeArg(value) : value)
			if (details.separatorEnd >= text.length || details.separatorEnd <= index)
				return values
			index = details.separatorEnd
		}
	}


	function isForHost(site, host) {
		const name = site.endsWith(ENTITY_ENDING) ? site.slice(0, -ENTITY_ENDING.length) : site
		const tail = site.endsWith(ENTITY_ENDING) ? host.slice(0, host.lastIndexOf('.')) : host
		return tail === name || tail.endsWith(`.${name}`)
	}


	// The rules for the host by their arguments, less any a list undoes for it
	function readRules(files, parser, host) {
		const rules = new Map()
		const undone = new Set()
		for (const [path, text] of Object.entries(files)) {
			if (!path.startsWith(LIST_PREFIX))
				continue
			for (const line of text.split('\n')) {
				const match = line.trim().match(RULE)
				if (!match)
					continue
				const sites = match[1].split(',').filter(Boolean)
				const included = sites.filter(site => !site.startsWith(EXCLUDED))
				const excluded = sites.filter(site => site.startsWith(EXCLUDED)).map(site => site.slice(EXCLUDED.length))
				if (!included.some(site => isForHost(site, host)) || excluded.some(site => isForHost(site, host)))
					continue
				const values = readArguments(parser, match[3])
				values[0] = values[0].replace(SCRIPT_EXTENSION, '')
				const key = JSON.stringify(values)
				if (match[2] === '#@#')
					undone.add(key)
				else
					rules.set(key, values)
			}
		}
		return [...rules].filter(([key]) => !undone.has(key)).map(([, values]) => values)
	}


	return (files, host) => {
		globalThis.self ??= globalThis
		const {scriptlets, ArglistParser} = loadLibrary(files)
		const byName = new Map()
		for (const scriptlet of scriptlets) {
			for (const name of [scriptlet.name, ...(scriptlet.aliases ?? [])])
				byName.set(name.replace(SCRIPT_EXTENSION, ''), scriptlet)
		}
		const functions = new Set()
		const addFunction = scriptlet => {
			for (const dependency of scriptlet.dependencies ?? [])
				addFunction(byName.get(dependency.replace(SCRIPT_EXTENSION, '')))
			functions.add(scriptlet.fn.toString())
		}
		const calls = []
		for (const [name, ...values] of readRules(files, new ArglistParser(ARGUMENT_SEPARATOR), host)) {
			const scriptlet = byName.get(name)
			if (!scriptlet)
				continue
			addFunction(scriptlet)
			calls.push(`try { ${scriptlet.fn.name}(${values.map(value => JSON.stringify(value)).join(', ')}) } catch {}`)
		}
		const script = `(() => {\nconst scriptletGlobals = {};\n${[...functions].join('\n')}\n${calls.join('\n')}\n})();\n`
		return {script, count: calls.length}
	}
})()
