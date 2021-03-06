<?php

/**
 * @file
 * Stores all the default handler functionality for OAI responses.
 */

/**
 * Responds to the ListSets request.
 *
 * @param array $params
 *   An array containing:
 *   -token (object): A resumption token object specifying information about
 *   a previous request, or a new empty one.
 *   -max_records (int): The maximum number of records to return per request.
 *
 * @return array
 *   An array containing:
 *   -total_records (int): The total amount of records for the query.
 *   -records (array): An array containing:
 *     -pid (string): The pid of the result
 *     -label (string): The label of the result.
 *
 * @see hook_islandora_esdn_oai_identify_request_handler()
 */
function islandora_esdn_oai_retrieve_sets($params) {
  $connection = islandora_get_tuque_connection();
  $records = array();
  $filters = array('sameTerm(?model, <info:fedora/islandora:collectionCModel>)');
  $sparql_query = islandora_esdn_oai_construct_sparql_query_for_sets($filters);
  $total_records = $connection->repository->ri->countQuery($sparql_query, 'sparql');
  $sparql_query .= <<<EOQ
  LIMIT {$params['max_records']}
  OFFSET {$params['token']->deliveredrecords}
EOQ;
  $query_results = $connection->repository->ri->sparqlQuery($sparql_query);
  module_load_include('inc', 'islandora', 'includes/dublin_core');
  foreach ($query_results as $result) {
    $object = islandora_object_load($result['object']['value']);
    $description = FALSE;
    if (isset($object['DC'])) {
      $dom = new DOMDocument();
      $dom->loadXML($object['DC']->content);
      $description = $dom->saveXML($dom->documentElement);
    }
    $pid = $result['object']['value'];
    $records[$pid] = array(
      'pid' => $pid,
      'label' => $result['title']['value'],
    );
    if ($description) {
      $records[$pid]['description'] = $description;
    }
  }
  return array(
    'total_records' => $total_records,
    'records' => $records,
  );
}

/**
 * Constructs the base object response XML depending on configuration options.
 *
 * @param array $params
 *   An array containing:
 *   -metadata_prefix (string): The metadata prefix of the request being
 *   executed.
 *   -pid (string): The pid of the record that we are building for.
 *
 * @return null|string
 *   XML string output of the record depending on configuration, NULL if an
 *   error was encountered.
 *
 * @see hook_islandora_oai_identify_request_handler()
 */
function islandora_esdn_oai_object_response_xml($params) {
  $object = islandora_object_load($params['pid']);
  $param = variable_get("islandora_oai_metadata_datastream_id_{$params['metadata_prefix']}", 'DC');
  if (isset($object[$param])) {
    $content = $object[$param]->content;
  }
  else {
    $content = NULL;
  }

  $oai_output = NULL;
  if (!empty($content)) {
    $self_transform_file = variable_get("islandora_oai_self_transform_file_{$params['metadata_prefix']}", 'default');
    if ($self_transform_file != 'default') {
      $self_xslt = new XSLTProcessor();
      $self_xsl = new DOMDocument();
      $self_xsl->load($self_transform_file);
      $self_input = new DOMDocument();
      $self_input->loadXML($content);
      $self_xslt->importStylesheet($self_xsl);
      $self_transform_params = module_invoke_all('islandora_oai_self_transform_params', $object, $params['metadata_prefix']);
      foreach ($self_transform_params as $namespace_uri => $transform_params) {
        $self_xslt->setParameter($namespace_uri, $transform_params);
      }

      $content = $self_xslt->transformToXML($self_input);
    }

    $xslt = new XSLTProcessor();
    $xsl = new DOMDocument();
    $file = variable_get("islandora_oai_transform_file_{$params['metadata_prefix']}", 'default');
    if ($file != 'default') {
      $xsl->load($file);
      $input = new DOMDocument();

      $did_load_ok = $input->loadXML($content);

      if ($did_load_ok) {
        $xslt->importStylesheet($xsl);
        $oai_output = $xslt->transformToXml($input);
      }
    }
    else {
      // Need to potentially remove the header information from the XML as
      // the response is embedded within XML.
      $document_dom = new DOMDocument();
      $document_dom->loadXML($content);
      $oai_output = $document_dom->saveXML($document_dom->documentElement);
    }
  }
  // Jam the dc.identifier.thumbnail into the OAI_DC output.
  if (isset($object['TN']) && variable_get('islandora_oai_append_dc_thumbnail', FALSE) && $params['metadata_prefix'] == 'oai_dc' && !empty($oai_output) && islandora_datastream_access(ISLANDORA_VIEW_OBJECTS, $object['TN'])) {
    $thumb_url = url("islandora/object/{$object->id}/datastream/TN/view/{$object->label}.jpg", array(
      'absolute' => TRUE,
      'language' => NULL,
    ));
    $dom = new DOMDocument();
    $dom->loadXML($oai_output);
    $dom->documentElement->appendChild($dom->createElementNS('http://purl.org/dc/elements/1.1/', 'dc:identifier.thumbnail', $thumb_url));
    $oai_output = $dom->saveXML($dom->documentElement);
  }

  if (variable_get("islandora_oai_include_object_links_for_{$params['metadata_prefix']}", FALSE) && !empty($oai_output)) {
    // See if we need to XPath to a specific spot to add it.
    $dom = new DOMDocument();
    $dom->loadXML($oai_output);
    $results = db_query('SELECT * FROM {islandora_oai_metadata_formats} WHERE metadata_prefix = :metadata_prefix', array(':metadata_prefix' => $params['metadata_prefix']));
    $record = $results->fetchObject();

    $object_url = url("islandora/object/{$object->id}", array(
      'absolute' => TRUE,
      'language' => NULL,
    ));
    $xpath = variable_get("islandora_oai_object_links_for_{$params['metadata_prefix']}_xpath", '');
    $field = variable_get("islandora_oai_object_links_for_{$params['metadata_prefix']}_field", '');
    $record_namespace = variable_get("islandora_oai_object_links_for_{$params['metadata_prefix']}_record_namespace", FALSE);

    if ($xpath) {
      $dom_xpath = new DOMXPath($dom);
      $dom_xpath->registerNamespace($record->record_prefix, $record->record_namespace);
      $dom_xpath->registerNamespace($record->metadata_prefix, $record->metadata_namespace);
      $elements = $dom_xpath->query($xpath);
      if ($elements->length == 0) {
        watchdog('islandora_oai', 'Unable to append object URL as could not find the element to XPath to for @pid. Error information: Metadata Prefix: @prefix, XPath: @xpath', array(
          '@pid' => $object->id,
          '@prefix' => $params['metadata_prefix'],
          '@xpath' => $xpath,
        ));
      }
      else {
        // If there's more than one element we'll just append to the first.
        foreach ($elements as $element) {
          if ($record_namespace) {
            $link_element = $dom->createElementNS($record->record_namespace, "{$record->record_prefix}:{$field}", $object_url);
          }
          else {
            $link_element = $dom->createElement($field, $object_url);
          }
          $element->appendChild($link_element);
          $oai_output = $dom->saveXML($dom->documentElement);
          break;
        }
      }
    }
    else {
      if ($record_namespace) {
        $link_element = $dom->createElementNS($record->record_namespace, "{$record->record_prefix}:{$field}", $object_url);
      }
      else {
        $link_element = $dom->createElement("$field", $object_url);
      }
      $dom->documentElement->appendChild($link_element);
      $oai_output = $dom->saveXML($dom->documentElement);
    }
  }

  // Transformation fails.
  if (empty($oai_output) && isset($object[$param])) {
    watchdog('islandora_oai', 'The pid @pidname failed to transform on the @datastream datastream with the file @file.',
      array(
        '@pidname' => $params['pid'],
        '@file' => $file,
        '@datastream' => $param,
      ), WATCHDOG_ERROR);
  }
  // Datastream doesn't exist.
  elseif (!isset($object[$param])) {
    watchdog('islandora_oai', "The pid @pidname failed to transform on the @datastream datastream as the datastream doesn't exist.",
      array(
        '@pidname' => $params['pid'],
        '@datastream' => $param,
      ), WATCHDOG_ERROR);
  }
  return $oai_output;
}

/**
 * Handles the GetRecord request.
 *
 * @param array $params
 *   An array containing:
 *   -pid (string): The pid of the record being requested.
 *
 * @return array|bool
 *   FALSE if the record does not exist, otherwise an array containing:
 *   -pid (string): The pid of the record.
 *   -date (string): The date timestamp of the record.
 *   -solr_doc (array): The Solr doc for the record.
 *
 * @see hook_islandora_oai_identify_request_handler()
 */
function islandora_esdn_oai_retrieve_record($params) {
  $pid = $params['pid'];
  $record = FALSE;
  $results = islandora_esdn_oai_query_solr(0, array('PID' => $pid, 'oai_real_PID' => $pid));
  if ($results['numFound'] > 0) {
    $solr_object = $results['objects'][0];
    $record = islandora_esdn_oai_construct_record_from_solr_response($solr_object);
  }
  return $record;
}

/**
 * Gets the setSpec membership information for a record.
 *
 * @param array $params
 *   An array containing:
 *   -record: An array containing:
 *     -pid: A string representation of the PID of the record.
 *     -date: A string representation of the date timestamp of the record.
 *     -solr_doc: An array containing the Solr doc for the record.
 *
 * @return array
 *   An array containing all set pids that this record belongs to.
 *
 * @see hook_islandora_oai_identify_request_handler()
 */
function islandora_esdn_oai_get_membership($params) {
  $sets = array();
  foreach (islandora_esdn_oai_get_membership_array() as $collection_field) {
    if (isset($params['record']['solr_doc'][$collection_field])) {
      // Need to cast to array such that we can check for multiple collections.
      foreach ((array) $params['record']['solr_doc'][$collection_field] as $set) {
        $sets[] = $set;
      }
    }
  }
  return $sets;
}

/**
 * Responds to the ListRecords or ListIdentifier request.
 *
 * @param array $params
 *   An array containing:
 *   -set: String of the PID of the set being searched for, FALSE if no set
 *   argument.
 *   -from: String of the from time starting point, FALSE if no from argument.
 *   -until: String of the until ending point, FALSE if no until argument.
 *   -token: Object representing a token.
 *
 * @return array
 *   An array describing the record retrieved from Solr.
 */
function islandora_esdn_oai_retrieve_records_or_identifiers($params) {
  $records = array();
  $set = $params['set'];
  $from = $params['from'];
  $until = $params['until'];
  $token = $params['token'];
  $query = array();
  $date_query = NULL;

  if ($from || $until) {
    $field= variable_get('islandora_oai_date_field', 'fgs_lastModifiedDate_dt');

    if ($from) {
      $solr_from = islandora_esdn_oai_datetime_to_solr_time($from);
    }
    else {
      $solr_from = '*';
    }

    if ($until) {
      $solr_until = islandora_esdn_oai_datetime_to_solr_time($until, TRUE);
    }
    else {
      $solr_until = '*';
    }
    $date_query = "$field:[$solr_from TO $solr_until]";
  }

  if (!isset($set)) {
    $set = NULL;
  }
  $results = islandora_esdn_oai_query_solr($token->deliveredrecords, $query, $set, $date_query);
  foreach ($results['objects'] as $solr_object) {
    $records[] = islandora_esdn_oai_construct_record_from_solr_response($solr_object);
  }
  return array(
    'total_records' => $results['numFound'],
    'records' => $records,
  );
}

/**
 * Helper function to mold the output from the queryProcessor to form.
 *
 * @param array $solr_object
 *   An array describing a result obtained from a Solr query.
 *
 * @return array
 *   An array describing the record retrieved from Solr.
 */
function islandora_esdn_oai_construct_record_from_solr_response($solr_object) {
  $pidtoreturn = isset($solr_object['solr_doc']['oai_real_PID']) ? (array) $solr_object['solr_doc']['oai_real_PID'] : (array) $solr_object['PID'];
  return array(
    'pid' => reset($pidtoreturn),
    'date' => $solr_object['solr_doc'][variable_get('islandora_oai_date_field', 'fgs_lastModifiedDate_dt')],
    'solr_doc' => $solr_object['solr_doc'],
    'tombstone' => isset($solr_object['solr_doc']['tombstone']) ? TRUE : FALSE,
  );
}

/**
 * Converts a time string into a form Solr can parse.
 *
 * @param string $datetime
 *   Time string to be parsed.
 * @param bool $until
 *   Whether an until time is specified.
 *
 * @return string
 *   A string representing the time in the given granularity.
 */
function islandora_esdn_oai_datetime_to_solr_time($datetime, $until = FALSE) {
  module_load_include('inc', 'islandora_oai', 'includes/request');
  $granularity = islandora_oai_get_date_granularity($datetime);
  $to_return = $datetime;

  switch ($granularity) {
    case 'YYYY-MM-DD':
      $to_return .= 'T00:00:00Z';
      if ($until) {
        $to_return .= '+1DAY-1MILLISECOND';
      }
      else {
        $to_return .= '/DAY';
      }
      break;

    case 'YYYY-MM-DDThh:mm:ssZ':
      if ($until) {
        $to_return .= '+1SECOND-1MILLISECOND';
      }
      else {
        $to_return .= '/SECOND';
      }
      break;

    default:
      // XXX: Maintain original behaviour of returning NULL... Should perhaps
      // throw an exception? Dunno.
      return;
  }

  return $to_return;
}

/**
 * Function that performs the queries to Solr.
 *
 * @param int $start
 *   The starting point for Solr queries, mainly used for resumption tokens.
 * @param array $query
 *   Associative array in the form of key = field; value = condition
 * @param bool $set
 *   Whether we are querying down for a set which requires walking in ITQL land.
 * @param bool $date_query
 *   Whether this is a date query or not.
 *
 * @return array
 *   The results generated from the crafted Solr query.
 */
function islandora_esdn_oai_query_solr($start = 0, $query = array(), $set = NULL, $date_query = NULL) {
  global $user;

  $query_processor = new IslandoraSolrQueryProcessor();

  // Build the query string.
  if (count($query) == 0) {
    $querystring = '*:*';
  }
  else {
    foreach($query as $field => &$value) {
      $value = $field . ':' . Apache_Solr_Service::escape($value);
      }
     $querystring = implode(' OR ', $query);
  }
  // display as fake as it gets
  $query_processor->buildQuery($querystring, array('display' => 'OAI ESDN')); 
  $fl_fields = islandora_esdn_oai_get_membership_array();
  $fl_fields[] = 'PID';
  $fl_fields[] = variable_get('islandora_oai_date_field', 'fgs_lastModifiedDate_dt');
  //Tombstone only fields
  $fl_fields[] = 'oai_real_PID';
  $fl_fields[] = 'tombstone';
  $query_processor->solrParams['fl'] = implode(',', $fl_fields);

  if ($date_query) {
    $query_processor->solrParams['fq'][] = $date_query;
  }

  $exclude_content_models = islandora_esdn_oai_get_restricted_models();
  $has_model = variable_get('islandora_oai_content_model_field', 'RELS_EXT_hasModel_uri_ms');
  foreach ($exclude_content_models as $content_model) {
    $content_model = trim($content_model);
    if ($content_model) {
      $query_processor->solrParams['fq'][] = '(-' . $has_model . ':("' . $content_model . '" OR "info:fedora/' . $content_model . '"))';
    }
  }
  if ($set) {
    // OAI uses : to separate multiple setSpecs so we had to replace it
    // from the pids earlier.
    $sets = explode(':', $set);
    $descendants = array();

    if ($user->uid === 0) {
      $user_name = 'anonymous';
    }
    else {
      $user_name = $user->name;
    }

    // Get all descendant children (collection and objs) and add to the sets
    // array if not already existing.
    // Cannot constrain on collections only as name conventions may change
    // across implementations.
    foreach ($sets as $key => $value) {
      // Replace the first occurrence of _ with a : so we are back to a
      // valid pid.
      $value = preg_replace('/_/', ':', $value, 1);
      $value = trim($value);
      $sets[$key] = $value;

      $connection = islandora_get_tuque_connection();
      if (!variable_get('islandora_risearch_use_itql_when_necessary', TRUE)) {
        $role_filters = array();
        foreach ($user->roles as $role) {
          $role_filters[] = "?role = \"$role\"";
        }
        $xacml_filters = array(
          '!bound(?role) && !bound(?user)',
          'bound(?role) && (' . implode(' || ', $role_filters) . ')',
          "bound(?user) && ?user = '$user_name'",
        );
        $parenthesis_wrap = function($string) {
          return "($string)";
        };
        $xacml_filter = implode(' || ', array_map($parenthesis_wrap, $xacml_filters));
        $query = <<<EOQ
SELECT ?child
FROM <#ri>
WHERE {
  ?child <fedora-model:state> <fedora-model:Active> .
  {
    ?child <fedora-model:hasModel> <info:fedora/islandora:collectionCModel>
  } UNION {
    ?child <fedora-model:hasModel> [<fedora-model:hasModel> <info:fedora/islandora:collectionCModel>]
  } .
  {
    ?child <fedora-rels-ext:isMemberOfCollection>+ <info:fedora/$value>
  } UNION {
    ?child <fedora-rels-ext:isMemberOf>+ <info:fedora/$value>
  }
  OPTIONAL {
    ?child <http://islandora.ca/ontology/relsext#isViewableByRole> ?role .
  }
  OPTIONAL {
    ?child <http://islandora.ca/ontology/relsext#isViewableByUser> ?user .
  }
  FILTER($xacml_filter)
}
EOQ;
        $query_results = $connection->repository->ri->sparqlQuery($query);
      }
      else {
        $itql_query = '
        select $child from <#ri>
        where
        (
          (
            (
            $child <fedora-model:hasModel> $model
            and $model <mulgara:is> <info:fedora/islandora:collectionCModel>
            and $model <fedora-model:hasModel> <info:fedora/fedora-system:ContentModel-3.0>
            )
          or
            (
            $child <fedora-model:hasModel> $model
            and $model <fedora-model:hasModel> <info:fedora/islandora:collectionCModel>
            and $child <fedora-model:state> <info:fedora/fedora-system:def/model#Active>
            )
          minus $child <http://islandora.ca/ontology/relsext#isViewableByRole> $role
          minus $child <http://islandora.ca/ontology/relsext#isViewableByUser> $user
          )
        or
          (
            (
            $child <fedora-model:hasModel> $model
            and $model <mulgara:is> <info:fedora/islandora:collectionCModel>
            and $model <fedora-model:hasModel> <info:fedora/fedora-system:ContentModel-3.0>
            )
          or
            (
            $child <fedora-model:hasModel> $model
            and $model <fedora-model:hasModel> <info:fedora/islandora:collectionCModel>
            and $child <fedora-model:state> <info:fedora/fedora-system:def/model#Active>
            )
        and
            (';
        foreach ($user->roles as $role) {
          $itql_query .= '$child <http://islandora.ca/ontology/relsext#isViewableByRole> ' . "'$role' or ";
        }
        $itql_query .= '$child <http://islandora.ca/ontology/relsext#isViewableByUser> ' . "'$user_name'" . ')';
        $itql_query .= ')
        )
      and
        (
        walk
          (
            $parent <fedora-rels-ext:isMemberOfCollection><info:fedora/' . $value . '>
             and $child <fedora-rels-ext:isMemberOfCollection> $parent
          )
        or
        walk
          (
            $parent <fedora-rels-ext:isMemberOf><info:fedora/' . $value . '>
            and $child <fedora-rels-ext:isMemberOf> $parent
          )
      )';

        $query_results = $connection->repository->ri->itqlQuery($itql_query);
      }
      foreach ($query_results as $result) {
        $walk_set = $result['child']['uri'];
        $walk_set = str_replace('info:fedora/', '', $walk_set);
        if (!in_array($walk_set, $descendants)) {
          $descendants[] = $walk_set;
        }
      }
      $descendants[] = $value;
    }
    $walked_sets = $descendants;

    $set_fq = array();
    // We are using OR here to account for fields in Solr that may index
    // just the PID or the entire URI. In the future if performance becomes
    // an issue with huge Solr queries we should re-visit this.
    foreach ($walked_sets as $walk) {
      foreach (islandora_esdn_oai_get_membership_array() as $collection_field) {
        $set_fq[] = $collection_field . ':("' . $walk . '" OR "info:fedora/' . $walk . '")';
      }
    }
    $query_processor->solrParams['fq'][] = '(' . implode(' OR ', $set_fq) . ')';
  }
  $query_processor->solrStart = $start;
  $query_processor->solrLimit = variable_get('islandora_oai_max_size', '20');
  try {
    $query_processor->executeQuery(FALSE);
    $solr_results = $query_processor->islandoraSolrResult['response'];
  }
  catch (Exception $e) {
    drupal_set_message(t('error searching @message', array('@message' => $e->getMessage())), 'error');
  }
  return $solr_results;
}

/**
 * Returns the collection fields defined in the admin page in array format.
 *
 * @return array
 *   An array containing the fields.
 */
function islandora_esdn_oai_get_membership_array() {
  // Store in a static variable, to avoid re-parsing within the same request.
  $mini_cache = &drupal_static(__FUNCTION__, array());

  if (empty($mini_cache)) {
    foreach (preg_split('/(\\r?\\n|\\r)+/', variable_get('islandora_oai_collection_field', "RELS_EXT_isMemberOfCollection_uri_ms\nRELS_EXT_isMemberOf_uri_ms")) as $spec) {
      $mini_cache[] = trim($spec);
    }
    $mini_cache = array_filter($mini_cache);
  }
  return $mini_cache;
}

/**
 * Get the restricted/excluded models.
 *
 * @return array
 *   An array of strings representing the models of which objects should not be
 *   returned from OAI requests.
 */
function islandora_esdn_oai_get_restricted_models() {
  $exclude_content_models = variable_get('islandora_oai_exclude_content_models', '');
  $cmodels = explode("\n", $exclude_content_models);
  $cmodels = array_map('trim', $cmodels);
  return array_filter($cmodels);
}

/**
 * Helper function to query for sets.
 *
 * @param array $filters
 *   An array of content models to be used as filters.
 * @param array $required
 *   An array of arrays containing:
 *   -vars: An array containing strings of the variables that are referenced
 *   in the required statements.
 *   -statements: An array containing string statements pertaining to the
 *   SPARQL query.
 *
 * @return string
 *   The SPARQL query to be executed.
 */
function islandora_esdn_oai_construct_sparql_query_for_sets($filters, $required = array()) {
  $sparql_query = <<<EOQ
  SELECT DISTINCT ?object ?title ?created !vars
  FROM <#ri>
  WHERE {
    ?object <fedora-model:hasModel> ?model ;
            <fedora-model:label> ?title ;
            <fedora-model:createdDate> ?created .
    !required
    !optionals
    !filters
  }
  ORDER BY ?title
EOQ;
  $optionals = (array) module_invoke('islandora_xacml_api', 'islandora_basic_collection_get_query_optionals', 'view');
  $filter_modules = array(
    'islandora_xacml_api',
    'islandora',
  );
  $excluded_models = islandora_esdn_oai_get_restricted_models();
  if (!empty($excluded_models)) {
    $map_excludes = function ($exclude) {
      return "sameTerm(?model, <info:fedora/$exclude>)";
    };
    $filters[] = format_string('!(!exclude_conjunction)', array(
      '!exclude_conjunction' => implode(' || ', array_map($map_excludes, $excluded_models)),
    ));
  }

  foreach ($filter_modules as $module) {
    $filters = array_merge_recursive($filters, (array) module_invoke($module, 'islandora_basic_collection_get_query_filters', 'view'));
  }

  // If the user is choosing to restrict things within the Islandora namespace
  // the filter needs to be modified as the islandora namespace is always
  // allowed by default.
  if (variable_get('islandora_namespace_restriction_enforced', FALSE) && variable_get('islandora_oai_exclude_islandora_namespace', FALSE)) {
    module_load_include('inc', 'islandora', 'includes/utilities');
    // This is somewhat of duplicated code but needs to be done so we remove
    // only the "islandora" namespace.
    $namespace_array = islandora_get_allowed_namespaces();
    $key = array_keys($namespace_array, 'islandora');
    $namespace_array = array_diff_key($namespace_array, array_flip($key));
    $namespace_sparql = implode('|', $namespace_array);
    $filters['islandora_namespace_restrictions'] = format_string('regex(str(?object), "info:fedora/(!namespaces):")', array('!namespaces' => $namespace_sparql));
  }
  $filter_map = function ($filter) {
    return "FILTER($filter)";
  };
  $sparql_query = format_string($sparql_query, array(
    '!vars' => !empty($required['vars']) ? implode(' ', $required['vars']) : '',
    '!required' => !empty($required['statements']) ? implode('. ', $required['statements']) : '',
    '!optionals' => !empty($optionals) ? ('OPTIONAL {{' . implode('} UNION {', $optionals) . '}}') : '',
    '!filters' => !empty($filters) ? implode(' ', array_map($filter_map, $filters)) : '',
  ));
  return $sparql_query;
}
